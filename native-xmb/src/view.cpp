#include "view.h"
#include "app_editor.h"
#include "host.h"
#include "pad.h"
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
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QTextStream>
#include <QTextBoundaryFinder>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTime>
#include <cmath>
#include <numbers>
#include <QSet>
#include <QScopedValueRollback>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#endif

namespace xmb {
namespace {
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
    setFocusPolicy(Qt::StrongFocus);setMinimumSize(800,450);
    setMouseTracking(true);qApp->installEventFilter(this);
    // Decode and cache frequently used original assets before animation starts.
    for(const auto& category:shell_.categories()) {
        assets_.image(category.container,category.texture);
        for(const auto& entry:category.entries)assets_.image(entry.container,entry.texture);
    }
    syncColumns();
    // The keyboard layout is resolved once, from the firmware, where the assets are already open: one RCO
    // through the P1.1 reader and one decrypted module (audit/OSK-FIRMWARE.md 12). A failed or absent firmware
    // read falls back to the committed table and logs which source it used, so the keyboard always has a
    // layout and the provenance is never guessed.
    oskLayout_=loadOskLayout(assets_,oskObjects_);
    monthImage_=assets_.image("qgl/lines",QString("textures/month_bg/rgb/%1.dds").arg(QDateTime::currentDateTime().date().month(),2,10,QChar('0')));
    connect(&shell_,&Shell::changed,this,[this]{++uiRevision_;syncColumns();loadWallpaper();update();});
    connect(&shell_,&Shell::mediaFrame,this,[this](const QImage& frame){
        mediaFrame_=frame;
        if(frame.isNull())photoViewport_={};
        else if(shell_.photoOpen())photoViewport_.reset(frame.size(),{1920,1080},shell_.photoFill());
        update();
    });
    connect(&shell_,&Shell::requestLibrary,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Choose media folder");shell_.noteActivity();if(!path.isEmpty())shell_.chooseLibrary(path);});
    connect(&shell_,&Shell::requestWallpaper,this,[this]{const auto file=QFileDialog::getOpenFileName(this,"Choose Wallpaper",{},"Images (*.png *.jpg *.jpeg *.bmp *.webp)");shell_.noteActivity();if(!file.isEmpty())shell_.chooseWallpaper(file);});
    connect(&shell_,&Shell::requestTheme,this,[this]{const auto file=QFileDialog::getOpenFileName(this,"Choose Theme",{},"PS3 Themes (*.p3t)");shell_.noteActivity();if(!file.isEmpty())shell_.chooseTheme(file);});
    connect(&shell_,&Shell::requestImportApps,this,[this]{AppEditor editor(shell_.appRegistryPath(),this);connect(&editor,&AppEditor::registryChanged,&shell_,&Shell::refreshContent);editor.exec();shell_.noteActivity();});
    connect(&shell_,&Shell::requestSystemName,this,[this](const QString& value){openOsk("System Name",value,[this](const QString& name){shell_.setSystemName(name);});});
    connect(&shell_,&Shell::requestNewUser,this,[this]{openOsk("Create New User",{},[this](const QString& name){shell_.createUser(name);});});
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
#endif
    // Input polling is independent of presentation. A 16 ms repaint timer produced ~62.5 Hz even on a 239 Hz
    // display. Request the next frame after Qt finishes composition/swap; swapInterval=1 remains in effect.
    connect(&timer_,&QTimer::timeout,this,[this]{pollController();});timer_.start();pad::start();
    // The PS button is watched globally: its own 20 ms HID poll keeps working while a game owns the
    // foreground, unlike pollController which is limited to the active XMB window.
    connect(&psWatcher_,&PsWatcher::pressed,this,&View::psPressed);
    connect(&psWatcher_,&PsWatcher::held,this,&View::psHeldEvent);
    connect(&psWatcher_,&PsWatcher::tapped,this,&View::psTappedEvent);
    connect(&psWatcher_,&PsWatcher::released,this,&View::psReleasedEvent);
    connect(&psWatcher_,&PsWatcher::cancelled,this,[this]{psPressActionable_=false;});
    psWatcher_.start();
    connect(this,&QOpenGLWidget::frameSwapped,this,[this]{if(isVisible())update();},Qt::QueuedConnection);
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
void View::PafEase::retarget(double target,double milliseconds,int mode) {
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
    duration_=frames/60.*.999999;
    from_=value;to_=target;progress_=0;elapsed_=0;running_=true;
}
void View::PafEase::advance(double seconds) {
    if(!running_)return;
    // FUN_00398748 modes 4/5: each canonical frame steps v = ((1-v) k + v) x growth.
    const auto step=[&](double v){return std::min(1.,((1.-v)*k_+v)*growth_);};
    // Sample the canonical 60 Hz curve at total elapsed time. Mutating the state with each fractional host tick
    // changes the curve at 120/240 Hz. Interpolation preserves its integer-frame values while allowing smooth
    // host refresh rates; the firmware's duration-end landing and retarget-from-current-value are retained.
    elapsed_+=std::max(0.,seconds);
    if(elapsed_>=duration_){progress_=1;running_=false;}
    else {
        const double frames=elapsed_*60;const int whole=int(frames);const double fraction=frames-whole;
        progress_=0;
        for(int i=0;i<whole;++i)progress_=step(progress_);
        progress_+=(step(progress_)-progress_)*fraction;
    }
    value=from_+(to_-from_)*progress_;
}
void View::syncColumns() {
    const int depth=shell_.depth();
    if(previousCategory_!=shell_.category()) {
        outgoing_=columns_;outgoingCategory_=previousCategory_;categoryFade_=outgoing_.isEmpty()?0.:1.;columns_.clear();
        fadeEase_=PafEase();fadeEase_.retarget(categoryFade_);fadeEase_.retarget(0,moveMilliseconds); // the old column cross-fades out
        previousCategory_=shell_.category();
    }
    // Keep the departed child until the return animation finishes. The shell's
    // actual page stack and selections change immediately and remain canonical.
    if(const qint64 key=(qint64(shell_.category())*64+depth)*65536+shell_.selectionAt(depth);key!=focusKey_ || !focusClock_.isValid()) {
        focusKey_=key;focusClock_.restart();
    }
    for(int i=0;i<=depth;++i) {
        if(i>=columns_.size())columns_.push_back(Column{shell_.entriesAt(i),shell_.selectionAt(i),double(shell_.selectionAt(i))});
        else {columns_[i].entries=shell_.entriesAt(i);columns_[i].selection=shell_.selectionAt(i);}
    }
    if(depth>previousDepth_)for(int i=previousDepth_+1;i<=depth;++i){columns_[i].position=columns_[i].selection;columns_[i].ease=PafEase();}
    previousDepth_=depth;
}
void View::loadWallpaper() {
    if(shell_.backgroundMode()!=2)return;
    // A PS3 theme's still background (decoded in memory by the shell) comes before the user's own wallpaper.
    if(!shell_.themeBackground().isNull()) {
        const auto key="p3t:"+shell_.themePath();if(key==wallpaperPath_)return;
        wallpaperPath_=key;++wallpaperRequest_;wallpaperImage_=shell_.themeBackground();return;
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
View::~View(){
#ifdef Q_OS_WIN
    if(volumeKeys_ && qApp)qApp->removeNativeEventFilter(volumeKeys_.get());
#endif
    saveFrameTrace();if(context()){makeCurrent();textRenderer_.release(*this);monthTexture_.reset();if(vao_)glDeleteVertexArrays(1,&vao_);background_.removeAllShaders();doneCurrent();}}
void View::initializeGL() {
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
in vec2 uv;out vec4 colour;uniform sampler2D layer;uniform float dimOpacity;uniform float boost;
void main(){colour=boost>0.0?texture(layer,uv)*boost:(dimOpacity>0.0?vec4(0,0,0,dimOpacity):texture(layer,uv));})"; // premultiplied; linear filtering at half scale = 2x2 box filter
    if(!uiComposite_.addShaderFromSourceCode(QOpenGLShader::Vertex,compositeVertex) || !uiComposite_.addShaderFromSourceCode(QOpenGLShader::Fragment,compositeFragment) || !uiComposite_.link())
        qCritical().noquote()<<"UI composite shader failed:"<<uiComposite_.log();
    particles_.init(*this,assets_);
    if(introEnabled_)coldboot_.init(*this,assets_);
    if(!monthImage_.isNull()) {
        monthTexture_=std::make_unique<QOpenGLTexture>(monthImage_.mirrored(false,true));
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
        const float fraction=float(date.day()-1)/date.daysInMonth();
        iconTint=monthTint(date.month())*(1-fraction)+monthTint(date.month()%12+1)*fraction;
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
        if(shell_.theme()>0 && shell_.theme()<int(std::size(fixedMonth)))
            when=QDateTime(QDate(when.date().year(),fixedMonth[shell_.theme()],1),when.time());
        if(when.date().month()!=texturesMonth_)loadMonthTextures(when.date().month());
        // Fixed colours select a background palette, not a different calendar for
        // amb.bmp. Icon material retains the real/diagnostic date set above.
        BackgroundTime time=backgroundTime(when);
        time.alpha=backgroundAlpha_;
        constexpr float coldboot1NightBias=.513834f; // BACKGROUND.mnu NIGHT WHIT BIAS, override/coldboot1 (coldboot2 = day)
        const float bootBias=coldboot1NightBias+(time.nightBrightness-coldboot1NightBias)*backgroundToColdboot2_;
        time.nightBrightness=bootBias+(time.nightBrightness-bootBias)*backgroundToClock_;
        presetBlend=float(std::clamp((time.nightDayBlend-(1.-kNightBlend))/kNightBlend,0.,1.));
        renderOriginalBackground(time,pixelWidth,pixelHeight);
        if(originalRibbon)ribbon_.render(*this,seconds,when.time().msecsSinceStartOfDay()/86400000.,pixelWidth,pixelHeight,scene);
    }
    glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
    if(original){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);} else glDisable(GL_BLEND);
    glViewport(0,0,pixelWidth,pixelHeight);
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
    if(originalParticles) { glBindFramebuffer(GL_FRAMEBUFFER,GLuint(scene)); particles_.render(*this,seconds,pixelWidth,pixelHeight); }
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
View::BackgroundTime View::backgroundTime(const QDateTime& when) {
    const auto clock=when.time();const double hour=clock.hour()+clock.minute()/60.+clock.second()/3600.,t=hour/24.;
    const auto date=when.date();
    BackgroundTime b;
    // _DayTime: a day-curve of t (FUN_00052a28 -> FUN_0004584c, whose arguments the decompile lost) x 2400. Only the
    // day half of the shader reads it (dark below ~400 and above ~2000, peak at 1200). Fitted: DAYSPREAD (2.68038) was
    // a linear spread 1200+(h-12)*100/DAYSPREAD, but that is too steep near noon — the firmware sets _DayTime 1168.77 at
    // 10:00 (RPCS3 XMB_FP_DUMP), not the linear 1125. Fit a cosine 1200*cos(k*(h-12)) through that point
    // (k = acos(1168.77/1200)/2), which also lands 06:00/18:00 at ~930 (~77% of midday, the reference's 75-85%).
    constexpr double daySpread=2.68038;
    const auto spread=[&](double h){return 1200.*std::cos(.114216*(h-12.));};
    static const double keys[][2]={{0,0},{3,380},{4.5,800},{6,spread(6)},{18,spread(18)},{19.5,1650},{21,2030},{24,2400}};
    double day=spread(hour);
    if(hour<6 || hour>18)for(size_t i=1;i<std::size(keys);++i)if(hour<=keys[i][0]){day=keys[i-1][1]+(keys[i][1]-keys[i-1][1])*(hour-keys[i-1][0])/(keys[i][0]-keys[i-1][0]);break;}
    b.day=float(day);
    // _NightTime: (t - 0.5, wrapped) x 2400, i.e. the night palette's own clock peaks at midnight.
    b.night=float(std::fmod(t+.5,1.)*2400.);
    // _MonthTime: frac(month) x 30; the integer part picks this/next month's textures (loadMonthTextures).
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
    glViewport(0,0,pixelWidth,pixelHeight);
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
            const QPoint at((k%columns)*tileW,(k/columns)*tileH);
            painter.drawImage(at,crop);painter.drawText(at+QPoint(6,16),QString("t+%1 s").arg(k*.5,0,'f',1));
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
    const qint64 frameStart=frameTracePath_.isEmpty()?0:clock_.nsecsElapsed();
    const qint64 nowNs=clock_.nsecsElapsed(),now=nowNs/1000000;
    const double dt=std::clamp(double(nowNs-previousFrameNs_)/1.e9,0.,.1);previousFrameNs_=nowNs;
    const double renderSeconds=secondsOverride_>=0.?secondsOverride_:double(nowNs)/1.e9; // fixed for A/B captures
    // Every move is a firmware paf animation (PafEase): 200 ms in accel mode 5 for the category and item moves and the
    // column cross-fade; the page (depth) move is slower, fitted to 300 ms in mode 4 (audit/PIPELINE.md "Menu motion").
    categoryEase_.retarget(shell_.category(),moveMilliseconds);categoryEase_.advance(dt);categoryPosition_=categoryEase_.value;
    if(!outgoing_.isEmpty()){fadeEase_.advance(dt);categoryFade_=fadeEase_.value;if(!fadeEase_.running()){categoryFade_=0;outgoing_.clear();}}
    depthEase_.retarget(shell_.depth(),pageMilliseconds,4);depthEase_.advance(dt);depthPosition_=depthEase_.value;
    for(auto& column:columns_){column.ease.retarget(column.selection,moveMilliseconds);column.ease.advance(dt);column.position=column.ease.value;}
    // The item-level Triangle options panel: its list slides in from the right over 200 ms (mode 5) and moves
    // between rows on the same curve. A fresh open lands on the current selection without sliding the list.
    if(shell_.optionsOpen()) {
        if(!optionsWasOpen_){optionsOpenEase_=PafEase();optionsOpenEase_.retarget(0);optionsPanelEase_=PafEase();}
        optionsOpenEase_.retarget(1,moveMilliseconds);
        optionsPanelEase_.retarget(shell_.optionsSelection(),moveMilliseconds);
    } else optionsOpenEase_.retarget(0,moveMilliseconds);
    optionsOpenEase_.advance(dt);optionsPanelEase_.advance(dt);
    optionsWasOpen_=shell_.optionsOpen();
    // The notification pop-up: Shell decides when one exists (queue + 4 s lifetime); here it slides and fades
    // in and out on the same 200 ms mode-5 curve the rest of the XMB uses. The content is latched so the last
    // frame of the exit still draws the text after Shell has already expired the entry.
    if(const auto* note=shell_.notification())notificationShown_={note->title,note->detail,note->icon};
    notificationEase_.retarget(shell_.notification()?1.:0.,moveMilliseconds);
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
    if(static const bool motionLog=qEnvironmentVariableIsSet("XMB_MOTION_LOG");motionLog && depthEase_.running())
        qInfo().noquote()<<"depth:"<<now<<"ms position"<<QString::number(depthPosition_,'f',4)<<"target"<<shell_.depth();
    if(std::abs(depthPosition_-shell_.depth())<.001) {
        depthPosition_=shell_.depth();
        if(columns_.size()>shell_.depth()+1)columns_.resize(shell_.depth()+1);
    }
    glClearColor(.04f,.04f,.05f,1.f);glClear(GL_COLOR_BUFFER_BIT);
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
    }
    if(glReady_) {
        const auto time=QTime::currentTime();
        renderBackground(float(renderSeconds),float(dayLight(time.hour()+time.minute()/60.+time.second()/3600.)),
            int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));
        coldboot_.render(*this,int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()),defaultFramebufferObject());
    }
    if(introRunning()){grabIntroFrame();++frames_;return;}
    // UI layer on the GPU: Qt's OpenGL paint engine draws into a 4x multisampled framebuffer at twice the window size;
    // it is resolved, then composited at half scale, where the bilinear fetch averages each 2x2 block. MSAA plus 2x
    // supersampling gives smooth glyph and icon edges. The layer is redrawn only when something visible changed.
    const QSize device(int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF())),pixels=device*2;
    // The keyboard's caret blink. 500 ms is the audit's FIT (OSK-FIRMWARE 5.2: the capture rig tops out near
    // 5 fps, so the period was never traced) and lives in OskCaret::blinkMilliseconds, not here. A pinned
    // --render-clock drives the phase as well as the background, so two captures of one state are identical
    // frames; a live run follows the wall clock.
    const qint64 blinkMs=secondsOverride_>=0?qint64(secondsOverride_*1000.):clock_.elapsed();
    if(osk_.open)osk_.caretLit=((blinkMs/qMax(1,oskLayout_.caret.blinkMilliseconds))%2)==0;
    QString signature=QString::asprintf("%.4f %.4f %.4f %d %d %d ",categoryPosition_,depthPosition_,categoryFade_,shell_.category(),shell_.depth(),uiRevision_);
    for(const auto& column:columns_)signature+=QString::number(column.position,'f',4)+' ';
    signature+=shell_.clockText()+shell_.message()+shell_.status();
    // The message surface's own fades run between shell changes (the box in and out, the hint row, the
    // error page's settle), so their amounts belong in the signature, quantised by 1/256.
    if(dialogEase_.value>.002 || dialogPlateEase_.value>.002 || dialogHintEase_.value>.002 || dialogSettleEase_.value>.002)
        signature+=QString::asprintf(" dialog %d %d %d %d ",
            int(dialogEase_.value*256.),int(dialogPlateEase_.value*256.),int(dialogHintEase_.value*256.),int(dialogSettleEase_.value*256.));
    // The options panel animates (slide-in, row focus) without a shell change on every frame.
    if(shell_.optionsOpen() || optionsOpenEase_.value>0.)
        signature+=QString::asprintf(" options %d %.4f %.4f %d",shell_.optionsOpen()?1:0,optionsOpenEase_.value,optionsPanelEase_.value,shell_.optionsSelection());
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
    if(notificationEase_.value>.002 && !notificationShown_.title.isEmpty())
        signature+=QString::asprintf(" notify %d ",int(notificationEase_.value*256.))+notificationShown_.title+'|'+
            notificationShown_.detail+'|'+notificationShown_.icon;
    // The volume HUD fades on its own clock between shell changes, so its opacity (quantised by 1/256, finer
    // than the 2x layer can show) and the value it is drawing belong in the signature.
    if(const auto hud=volumeHud_.state(clock_.elapsed());hud.visible) {
        signature+=QString::asprintf(" vol %d %d %d ",int(hud.opacity*256.),volumeLevel_,volumeMuted_?1:0);
        // Opt-in trace of the fade itself: one line per frame while it runs, so the 3000/250 ms boundary the
        // video controls are specified at is measurable here without landing a capture inside a 250 ms window.
        if(volumeTrace() && hud.opacity<1.)
            qInfo().noquote()<<QString("volume: trace HUD fade opacity %1 at %2 ms after the change")
                .arg(hud.opacity,0,'f',3).arg(clock_.elapsed()-volumeHudAt_);
    }
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
        uiSignature_=signature;
        uiMsaa_->bind();glViewport(0,0,pixels.width(),pixels.height());glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        // Glyphs are rasterised for the layer's own scale (twice the window): at the window's scale they were
        // magnified 2x by bilinear sampling before the 2x2 resolve, which left all text soft.
        textRenderer_.beginFrame(*this,pixels,std::min(pixels.width()/1920.,pixels.height()/1080.),2);
        {
            QOpenGLPaintDevice paintDevice(pixels);
            QPainter ui(&paintDevice);
            ui.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
            const double scale=std::min(pixels.width()/1920.,pixels.height()/1080.);
            ui.translate((pixels.width()-1920*scale)/2,(pixels.height()-1080*scale)/2);ui.scale(scale,scale);drawScene(ui);
        }
        QOpenGLFramebufferObject::blitFramebuffer(uiResolved_.get(),uiMsaa_.get());
    }
    // Category icons: the firmware glass shader, refracting the finished background (each cut before its own draw).
    if(glReady_ && icons_.ready())icons_.render(*this,iconQueue_,renderSeconds,presetBlend_,device.width(),device.height(),defaultFramebufferObject());
    // Composite (premultiplied alpha) over the background in the widget's framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());glViewport(0,0,device.width(),device.height());
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    uiComposite_.bind();uiComposite_.setUniformValue("layer",0);uiComposite_.setUniformValue("dimOpacity",0.f);
    uiComposite_.setUniformValue("boost",0.f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,uiResolved_->texture());
    glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);
    // Native approximation: dim the complete composed image; no host brightness or power changes.
    if(shell_.screensaverMinutes()>0 && shell_.idleDisplayAllowed()) {
        const qint64 age=shell_.idleMilliseconds()-qint64(shell_.screensaverMinutes())*60000;
        const float dim=.55f*float(std::clamp(double(age)/750.,0.,1.));
        if(dim>0){uiComposite_.setUniformValue("dimOpacity",dim);glDrawArrays(GL_TRIANGLES,0,3);}
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D,0);uiComposite_.release();glDisable(GL_BLEND);
    grabIntroFrame();
    if(!frameTracePath_.isEmpty() && frameTrace_.size()<20000) {
        bool moving=categoryEase_.running() || depthEase_.running() || fadeEase_.running();
        for(const auto& column:columns_)moving|=column.ease.running();
        frameTrace_.push_back({frameStart,clock_.nsecsElapsed()-frameStart,false,rebuild,moving});
    }
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
// The item-icon glass mask, prepared once per texture. The firmware draws these glyphs with the icon program:
// alpha is the mask, and the program turns it into a refracted surface whose flat interior is almost the frame
// itself (measured on the stored RPCS3 root frame: the folder body reads as 0.18 of white over the background)
// with a bright rim at the mask edge (the program's Fresnel term). A plain alpha blend shows the whole mask
// (0.70 there), which is what made the folder rows read as flat grey slabs instead of blue/gold glass.
const QImage& View::itemGlyph(const QString& container,const QString& texture) {
    const QString key=container+"/"+texture;
    if(const auto it=itemGlyphs_.constFind(key);it!=itemGlyphs_.constEnd())return it.value();
    const QImage& source=assets_.bestImage(container,texture);
    if(source.isNull())return itemGlyphs_.insert(key,QImage()).value();
    QImage argb=source.convertToFormat(QImage::Format_ARGB32);
    // Opaque artwork (the PS Store bag, ICON0 stills, disc art) keeps its own pixels: its mask covers its
    // footprint opaquely. A glass glyph's body is a translucent mask (median coverage well below opaque) and only
    // its rim reaches 250, so the median of the covered pixels tells the two apart.
    int histogram[256]{};
    qint64 covered=0;
    for(int y=0;y<argb.height();++y) {
        const auto* row=reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for(int x=0;x<argb.width();++x){const int alpha=qAlpha(row[x]);++histogram[alpha];if(alpha)++covered;}
    }
    int median=0;
    if(covered)for(int total=0,alpha=1;alpha<256;++alpha){total+=histogram[alpha];if(total>=covered/2){median=alpha;break;}}
    if(median>=230)return itemGlyphs_.insert(key,argb).value();
    // The icon program reflects the frame through the mask, so the white it adds falls as the mask thickens: a
    // folder's body reads as ~0.18 of white over the background and its emblem (a thicker region) nearly
    // disappears into the frame. Measured on the stored RPCS3 root frame: the plain folder's 196/255 body shows as
    // 0.18 of white, i.e. k*(255-196). The remaining term is the program's Fresnel highlight, which the mask does
    // not carry, so the silhouette (the pixels that touch the transparent outside) keeps the mask's own alpha.
    static constexpr double kThin=.78;
    const int w=argb.width(),h=argb.height();
    QVector<quint8> outside(size_t(w)*size_t(h),0);
    QVector<int> pending;
    const auto seed=[&](int x,int y){const size_t at=size_t(y)*size_t(w)+size_t(x);if(!outside[at] && !qAlpha(reinterpret_cast<const QRgb*>(argb.constScanLine(y))[x])){outside[at]=1;pending.push_back(y*w+x);}};
    for(int x=0;x<w;++x){seed(x,0);seed(x,h-1);}
    for(int y=0;y<h;++y){seed(0,y);seed(w-1,y);}
    while(!pending.isEmpty()) {
        const int index=pending.takeLast(),x=index%w,y=index/w;
        if(x>0)seed(x-1,y);
        if(x+1<w)seed(x+1,y);
        if(y>0)seed(x,y-1);
        if(y+1<h)seed(x,y+1);
    }
    const auto isOutside=[&](int x,int y){return x<0||y<0||x>=w||y>=h||outside[size_t(y)*size_t(w)+size_t(x)];};
    // The rim is ~4 texture pixels thick so that it survives the downscale to a 60..105 px row (a 1 px rim would
    // vanish under the smoothing).
    QVector<quint8> rim(size_t(w)*size_t(h),0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        if(qAlpha(reinterpret_cast<const QRgb*>(argb.constScanLine(y))[x]) &&
           (isOutside(x-1,y)||isOutside(x+1,y)||isOutside(x,y-1)||isOutside(x,y+1)))rim[size_t(y)*size_t(w)+size_t(x)]=1;
    for(int pass=0;pass<3;++pass) {
        QVector<quint8> grown=rim;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const size_t at=size_t(y)*size_t(w)+size_t(x);
            if(rim[at] || !qAlpha(reinterpret_cast<const QRgb*>(argb.constScanLine(y))[x]))continue;
            const auto lit=[&](int nx,int ny){return nx>=0&&ny>=0&&nx<w&&ny<h&&rim[size_t(ny)*size_t(w)+size_t(nx)];};
            if(lit(x-1,y)||lit(x+1,y)||lit(x,y-1)||lit(x,y+1))grown[at]=1;
        }
        rim=grown;
    }
    QImage prepared(argb.size(),QImage::Format_ARGB32);
    prepared.fill(Qt::transparent);
    for(int y=0;y<h;++y) {
        const auto* in=reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        auto* out=reinterpret_cast<QRgb*>(prepared.scanLine(y));
        for(int x=0;x<w;++x) {
            const int alpha=qAlpha(in[x]);
            if(!alpha)continue;
            const int coverage=rim[size_t(y)*size_t(w)+size_t(x)]?alpha:std::clamp(int((255-alpha)*kThin),0,255);
            out[x]=qRgba(qRed(in[x]),qGreen(in[x]),qBlue(in[x]),coverage);
        }
    }
    return itemGlyphs_.insert(key,prepared).value();
}
void View::drawItemGlyph(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal opacity) {
    const auto& image=itemGlyph(container,texture);
    if(image.isNull())return;
    // The mask footprint is the same before and after preparation, so the cached alpha bounds of the source fit
    // the same rect (the prepared body alpha falls below the bounds threshold, which would shrink it).
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
void View::drawNineSlice(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal inset,qreal opacity) {
    // tex_notification is a 64x64 tile: a dark fill with a lighter 1 px border and rounded corners. Scaling it
    // to the plaque's aspect would stretch the corner arcs into ellipses, so the 3x3 parts are drawn with the
    // corners at their own pixel size and only the edges and the centre stretched.
    const auto& image=assets_.image(container,texture);
    if(image.isNull())return;
    const qreal i=std::min<qreal>(inset,std::min(image.width(),image.height())/2.);
    if(rect.width()<2*i || rect.height()<2*i)return;
    const qreal sx[4]={0.,i,qreal(image.width())-i,qreal(image.width())};
    const qreal sy[4]={0.,i,qreal(image.height())-i,qreal(image.height())};
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
void View::drawScene(QPainter& p) {
    iconQueue_.clear();
    QFont font(shell_.fontFamily());font.setPixelSize(27);p.setFont(font);
    // The message surface is an explicit kind the shell decides (Shell::MessageKind, audit/DIALOGS-FIRMWARE.md
    // section 4.2 item 1): an inline empty-state line is a list row, a titled page / modal box / error page /
    // information page is a full screen the message itself owns. Nothing is inferred from the string any more.
    const MessageKind messageKind=shell_.message().isEmpty()?MessageKind::None:shell_.messageKind();
    const bool messageUp=messageKind!=MessageKind::None;
    const bool inlineMessage=messageKind==MessageKind::Inline;
    // A Page/Box/ErrorCode/Info surface replaces the item list and the category band (the firmware's page is
    // the whole screen: the reference frame's band is off-frame, and neither measured frame dims anything).
    const bool messageScreen=messageUp && !inlineMessage;
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
                                    const QVector<MenuOption>* rows=nullptr) {
        const double x0=1330+ahead*600;
        const double rowCount=rows?double(rows->size()):double(items.size());
        const double first=505-(rowCount-1.)*20.;
        p.save();p.setOpacity(alpha);
        // Over the XMB's own background the light strip is enough. Over video it is not: white 23 px text
        // on a bright frame is unreadable, so the in-player panel lays the strip on a dark plate (the
        // console's control panel dims the picture behind it for the same reason).
        if(rows) {
            QLinearGradient shade(x0,0,1920,0);
            shade.setColorAt(0,QColor(0,0,0,158));shade.setColorAt(1,QColor(0,0,0,120));
            p.fillRect(QRectF(x0,0,1920-x0,1080),shade);
        }
        QLinearGradient fill(x0,0,1920,0);fill.setColorAt(0,QColor(255,255,255,46));fill.setColorAt(.08,QColor(255,255,255,30));fill.setColorAt(1,QColor(255,255,255,22));
        p.fillRect(QRectF(x0,0,1920-x0,1080),fill);
        QLinearGradient edge(x0-10,0,x0+4,0);edge.setColorAt(0,QColor(255,255,255,0));edge.setColorAt(.75,QColor(255,255,255,70));edge.setColorAt(1,QColor(255,255,255,0));
        p.fillRect(QRectF(x0-10,0,14,1080),edge);
        // The list scrolls with the focused row, so the heading and the note follow the row they belong
        // to rather than the list's resting box: `first` is only where row 0 sits when focus is at 0.
        const double rowZero=505-position*40;
        if(!heading.isEmpty()) { // a submenu (Sort By) or the player panel names itself above its list
            const double headingY=rows?std::max(56.,rowZero-68.):first-64;
            text({x0+(rows?34:16),headingY,1920-x0-40,40},heading,19,QColor(255,255,255,190));
        }
        const double labelX=x0+(rows?34:16),labelWidth=1920-labelX-24;
        for(int i=0;i<int(rowCount);++i) {
            const double distance=i-position,y=505+distance*40;
            if(y<-40 || y>1120)continue;
            const double focus=std::max(0.,1.-std::abs(distance));
            const QRectF rect(labelX,y-20,labelWidth,40);
            const bool enabled=rows?(*rows)[i].enabled:true;
            const QString title=rows?(*rows)[i].title:items[i];
            // A row this build cannot honour still takes the focus halo (weaker), so it is visibly the
            // selected row; its title is dimmed and its note says why.
            if(focus>.5)glow(rect,title,23,(focus-.5)*2*(enabled?1.:.45));
            if(rows && (*rows)[i].active) { // the current choice, where the console's own list shows a check
                p.save();p.setBrush(QColor(255,255,255,235));p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(x0+18,y),4.5,4.5);p.restore();
            }
            const int level=rows&&!enabled?115:int(255*(.78+.22*focus));
            text(rect,title,23,QColor(255,255,255,level));
            if(rows && focus>.5) {
                // One line under the list while a row has focus: the file behind a subtitle row, or the
                // reason a row cannot be chosen. Never a claim the shell has not measured.
                const QString note=(*rows)[i].note;
                if(!note.isEmpty())
                    text({labelX,std::min(1010.,rowZero+rowCount*40.+8),labelWidth,90},note,16,QColor(225,225,225,235),
                        Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
            }
        }
        p.restore();
    };
    if(shell_.backgroundMode()==2 && !wallpaperImage_.isNull()) {
        // Covers the whole window, not just the 16:9 scene, so the bars beside a letterboxed scene show the picture too.
        const QRectF window=p.transform().inverted().mapRect(QRectF(0,0,p.device()->width(),p.device()->height())).united(QRectF(0,0,1920,1080));
        const auto size=QSizeF(wallpaperImage_.size()).scaled(window.size(),Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRectF(window.center()-QPointF(size.width()/2,size.height()/2),size),wallpaperImage_);
        p.fillRect(window,QColor(0,0,0,35));
    }
    if(shell_.mediaOpen()) {
        const auto now=clock_.elapsed();
        if(!previousMediaOpen_ || previousMediaTitle_!=shell_.mediaTitle())mediaControls_.opened(now);
        previousMediaOpen_=true;previousMediaTitle_=shell_.mediaTitle();
        mediaControls_.setPaused(shell_.mediaPaused() || shell_.busy(),now);
        if(!mediaFrame_.isNull()) {
            p.fillRect(QRectF(0,0,1920,1080),Qt::black);
            const auto size=QSizeF(mediaFrame_.size()).scaled(QSizeF(1920,1080),shell_.photoOpen() && shell_.photoFill()?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);
            p.save();p.setClipRect(QRectF(0,0,1920,1080));
            p.drawImage(shell_.photoOpen() && photoViewport_.valid()?photoViewport_.imageRect():QRectF(QPointF((1920-size.width())/2,(1080-size.height())/2),size),mediaFrame_);
            p.restore();
        }
        // The active sidecar subtitle cue. Drawn over the video, below the controls in z-order, and only
        // while no dialog is up (a dialog's own black plate would otherwise have text over it). The PS
        // menu paints the whole frame black later in this function, so it hides the cue without a
        // special case here.
        if(shell_.videoOpen() && shell_.message().isEmpty()) {
            const auto cue=shell_.subtitleCueText();
            if(!cue.isEmpty())drawSubtitle(p,cue);
        }
        p.save();p.setOpacity(shell_.videoOpen()?mediaControls_.state(now).opacity:1.);
        text({110,970,1500,50},shell_.mediaTitle(),27,Qt::white);
        text({110,1020,1700,35},shell_.photoOpen()?QString("Circle Back   Cross ")+(shell_.slideshowActive()?"Pause":"Slideshow")+"   L1/R1 or +/- Zoom   Square/0 Reset   Triangle/Q/E Rotate   Arrows Pan when zoomed   [/] Photo":"Circle Back   Cross Play/Pause   Left/Right Seek   L1/R1 or [/] Track   Square/M Shuffle   R Repeat   Triangle Options",21,QColor(230,230,230));
        if(shell_.busy())text({110,910,1000,45},"Loading…",26,Qt::white);
        else if(!shell_.photoOpen())text({110,895,1700,36},shell_.mediaQueueSummary(),21,QColor(230,230,230));
        else if(photoViewport_.valid())text({110,895,1700,36},QString("Zoom %1%").arg(photoViewport_.zoomPercent(),0,'f',0),21,QColor(230,230,230));
        if(!shell_.photoOpen() && shell_.mediaDuration()>0) {
            const double ratio=std::clamp(double(shell_.mediaPosition())/shell_.mediaDuration(),0.,1.);
            p.fillRect(QRectF(110,941,1700,4),QColor(255,255,255,60));p.fillRect(QRectF(110,941,1700*ratio,4),Qt::white);
            const auto time=[](qint64 ms){return MediaControls::formatTime(ms);};
            text({1450,963,360,45},time(shell_.mediaPosition())+" / "+time(shell_.mediaDuration()),23,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
        }
        p.restore();
        // The in-player panel (Triangle): the same right-hand options panel, over the video and over the
        // controls' band, so it reads as an overlay that owns the screen while it is open. It changes
        // nothing about the controls' own clock: mediaControls_ was advanced above exactly as before.
        if(shell_.optionsArePlayer() && optionsOpenEase_.value>0.) {
            drawOptionsPanel(QStringList{},optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,
                shell_.optionsTitle(),&shell_.options());
        }
    } else {
        previousMediaOpen_=false;
        const auto& categories=shell_.categories();
        const double childAmount=std::clamp(depthPosition_,0.,1.);
        for(int i=0;i<categories.size() && !messageScreen;++i) {
            // Firmware geometry (RPCS3 capture at 1920x1080, audit/PIPELINE.md): focus column at x 565, icons 200 px apart
            // with 10 px more either side of the focused one, centres y 290 (focused 280); quads 117 px, focused 167 px;
            // shader brightness 0.698 unfocused, 1 focused. Inside a page (reference 95 s) the open category steps 262 px
            // left (traced: RCO xmenu posX -395 and the captured open-page x 303, audit/LAYOUT-TRACE.md); the ones to its
            // right stay faintly (~0.1) on a 186 px pitch, and the ones to its left leave.
            const auto& cat=categories[i];const double distance=i-categoryPosition_;            const bool open=i==shell_.category();
            const double level0X=kColumnX+distance*200+10*std::clamp(distance,-1.,1.);
            // Open-page recession (traced 2026-10-27, RPCS3 burst): the bar keeps its 200-unit pitch at the
            // recorded parent distance 2857.2, i.e. 200*2700/2857.2 = 189.0 px (measured 190.5 px in frames);
            // the old 186 was a fit. The 0.945 scale is the same recession.
            const double x=open || distance<0?level0X-childAmount*262:level0X+(kColumnX-40+distance*189.0-level0X)*childAmount;
            const double focus=std::max(0.,1.-std::abs(distance));
            // No dim behind a message: both measured reference frames keep the XMB at full brightness
            // (audit 4.1 "frame dim behind a message"), so the 0.18 and 135-alpha washes are gone.
            const double opacity=open?1.:(1.-childAmount)+(distance>0?.1*childAmount:0.);
            p.save();p.setOpacity(opacity);
            if(const auto themed=shell_.themeIcon(cat.themeIcon);!themed.isNull()) {
                // A PS3 theme's icon keeps its own colours and takes the quad of the firmware's glass icon.
                const double size=117.13+50.2*focus;p.setOpacity(opacity*(.698+.302*focus));
                p.drawImage(QRectF(x-size/2,290-10*focus-size/2,size,size),themed);
            }
            else if(cat.container=="qgl/icontex" && icons_.ready())
                iconQueue_.push_back({cat.texture,{x,290-10*focus},117.13+50.2*focus,.698+.302*focus,opacity});
            else {const double size=76+focus*16;p.setOpacity(opacity*(.67+.33*focus));drawCategory(p,cat,{x-size/2,290-size/2,size,size});}
            if(i==shell_.category()){p.setOpacity((1.-childAmount));text({x-140,335,280,40},cat.title,21,Qt::white,Qt::AlignCenter);}
            p.restore();
        }
        const auto isChoiceColumn=[](const Column& column) {
            if(column.entries.isEmpty())return false;
            for(const auto& entry:column.entries)if(entry.action!=Action::SetSetting)return false;
            return true;
        };
        const auto drawColumns=[&](const QVector<Column>& columns,double opacity,double offset,bool active) {
        // An open options panel is not a level of its own: the columns keep the layout of the page under it.
        const double panelOpen=columns.size()>=2 && isChoiceColumn(columns.back())?std::clamp(depthPosition_-(columns.size()-2),0.,1.):0.;
        const double layoutDepth=depthPosition_-panelOpen;
        const int lastLevel=int(columns.size())-1-(panelOpen>0?1:0);
        for(int level=0;level<columns.size();++level) {
            const auto& column=columns[level];
            const bool panelColumn=isChoiceColumn(column);
            double behind=std::clamp((panelColumn?depthPosition_:layoutDepth)-level,0.,1.);
            const double ahead=std::clamp(level-(panelColumn?depthPosition_:layoutDepth),0.,1.);
            const double columnAlpha=(1.-ahead)*opacity;
            // A setting's choices open as the PS3 options panel on the right instead of a new column.
            if(panelColumn) {
                if(columnAlpha>0) {
                    QStringList titles;titles.reserve(column.entries.size());
                    for(const auto& entry:column.entries)titles<<entry.title;
                    drawOptionsPanel(titles,column.position,columnAlpha,ahead,QString());
                }
                continue;
            }
            // The list under an open options panel stays in place; unfocused rows drop to icons only.
            const double panelAmount=level+1<columns.size() && isChoiceColumn(columns[level+1])?panelOpen:0.;
            if(behind>.999 && level<lastLevel-1)continue;
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
                // the focused row stays at 2700 and steps 262 px left, the others 110 px.
                const double depth=behind*(1.-focus);
                const double y=540+(505+row-540-15*depth*std::clamp(distance,0.,1.))*(1.-.055*depth);
                if(y<-100 || y>1050)continue;
                const double iconX=kColumnX-behind*(110+152*focus)+ahead*250+offset;
                // Reference (RPCS3 4.x video): square icons ~60 px, ~105 px when focused; utility folders are icons too.
                // Only media thumbnails (album art) keep a wide tile.
                // Setting rows carry the small round settings bullet: 18 px, 39 px focused (reference 95 s).
                const bool bullet=entry.container=="qgl/icontex" && entry.texture=="23n";
                const double size=bullet?18+focus*21:level==0?60+focus*44:40+focus*16;
                // Firmware folder glyphs: a folder row in the Game column carries the emblem of what it holds - the
                // captured Game Data Utility row is item_tex_ps3game_folder (folder + gamepad, explore_category_game.rco
                // and explore_plugin_full.rco), not the bare storage folder. Steam's All Games / collection / Not
                // Installed rows are the same row, so they take that glyph. The media columns ship no emblem folder
                // at all: explore_category_video.rco's only folder is item_tex_plain_folder, which is what local
                // library folders keep.
                const QString itemTexture=entry.texture=="item_tex_plain_folder" && entry.action==Action::SteamFolder?
                    QStringLiteral("item_tex_ps3game_folder"):entry.texture;
                const bool tile=entry.gameTile || itemTexture.contains("album") || entry.container=="file"; // ICON0 thumbnails are wide
                // A row behind an open page is the parent column, and every captured parent-column row is an item icon
                // (77.616 unfocused / 184.800 focused, 2857.2/0.247 below focus). Keeping the wide album/ICON0 card
                // there drew a 320x180 placeholder on the same spot as the open category icon (Video Library over the
                // film strip), so a behind row is drawn at the item-icon box of its own level, art or not.
                const double quad=itemIconSize(level,bullet,focus);
                double iconWidth=tile?155+focus*165:size,iconHeight=tile?90+focus*90:size;
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
                        const QSizeF fitted=QSizeF(entry.artwork.size()).scaled(card.size(),Qt::KeepAspectRatio);
                        p.save();p.setOpacity(iconAlpha);p.drawImage(QRectF(card.center()-QPointF(fitted.width()/2,fitted.height()/2),fitted),entry.artwork);p.restore();
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
                p.save();p.setOpacity(columnAlpha*(1.-behind)*(1.-panelAmount*(1.-focus))*(1.-idleFade));
                // An inline empty-state line stands in for the focused row's own label slot: the firmware's
                // line sits where the list row would be (measured x 630, centre y 504), so the row keeps its
                // icon and focus but not its title. No other row is affected.
                const bool inlineStandIn=inlineMessage && active && level==shell_.depth() && i==column.selection;
                // One label column per level, as on the PS3 (reference: icon centre +99 at the top level, +57 inside a page).
                const double labelX=kColumnX+(level==0?(tile?173:99):57)+ahead*250+offset;
                const double alpha=(.72+.28*focus)*(entry.disabled?.55:1.);
                // A row's own value wins when it sets one (the sign-in page's fields: server URL, user name and
                // the password as dots). Every other row leaves Entry::fieldValue empty, so this falls through to
                // Shell::valueText() exactly as before and no existing frame changes.
                const auto value=active?(entry.fieldValue.isEmpty()?shell_.valueText(entry):entry.fieldValue):QString{};
                QFont labelFont(shell_.fontFamily());labelFont.setPixelSize(27);
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
                    if(focus>.5)glow(labelRect,label,27,(focus-.5)*2*breath*(entry.disabled?.45:1.));
                    text(labelRect,label,27,QColor(255,255,255,int(255*alpha)));
                    if(elided.elided)
                        text(QRectF(labelRect.left()+elided.ellipsisX,labelRect.top(),40,labelRect.height()),
                             QString(QChar(0x2026)),27,QColor(255,255,255,int(255*alpha)));
                    if(!value.isEmpty() && panelAmount<.5) {
                        QString shown=value;
                        if(entry.action==Action::JellyfinSignInField) {
                            QFont valueFont(shell_.fontFamily());valueFont.setPixelSize(21);
                            if(textRenderer_.ready()) {
                                const auto clipped=textRenderer_.elideToWidth(valueFont,value,440);
                                shown=clipped.elided?clipped.prefix+QString(QChar(0x2026)):value;
                            } else shown=QFontMetrics(valueFont).elidedText(value,Qt::ElideRight,440);
                        }
                        text({1300+offset,y-25,440,50},shown,21,QColor(255,255,255,int(230*alpha*(1.-2*panelAmount))));
                    }
                    const auto subtitle=shell_.subtitleText(entry);
                    if(!subtitle.isEmpty())text({labelX,y+20,1100,30},subtitle,17,QColor(230,230,230,int(235*alpha)),Qt::AlignLeft|Qt::AlignTop);
                    // The help line belongs to the selection, not the eased position: on the real XMB it moves to the new
                    // row on the frame the move starts, fully opaque, including on every held-key repeat (RPCS3 capture).
                    else if(i==column.selection && !entry.detail.isEmpty())text({labelX,y+24,1100,70},entry.detail,18,QColor(225,225,225),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
                }
                p.restore();
            }
            if(level>0 && columnAlpha>0 && panelAmount<.999) {
                p.save();p.setOpacity(columnAlpha*(1.-behind));text({kColumnX-142+ahead*250,480,50,50},QString(QChar(0x2039)),42,Qt::white,Qt::AlignCenter);p.restore();
            }
        }
        };
        // The item columns are replaced by a message screen and kept behind an inline line, which stands
        // in only for the focused row's label (the firmware's line sits in that list slot).
        if(!messageScreen) {
            // On a category move the item columns do not travel a whole icon step (202 px): the reference slides the old
            // list ~150 px away while it fades and brings the new one in from ~150 px, crossing over each other.
            if(!outgoing_.isEmpty())drawColumns(outgoing_,categoryFade_,(outgoingCategory_-categoryPosition_)*150,false);
            drawColumns(columns_,1.-categoryFade_,(shell_.category()-categoryPosition_)*150,true);
            // Item-level Triangle options: the same right-hand panel over the page it belongs to. Only drawn while
            // the shell reports it open; the panel never becomes a level of the column stack.
            if(shell_.optionsOpen() && optionsOpenEase_.value>0.) {
                QStringList titles;titles.reserve(shell_.options().size());
                for(const auto& option:shell_.options())titles<<option.title;
                drawOptionsPanel(titles,optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,shell_.optionsTitle());
            }
        }
        if(!messageScreen && !shell_.status().isEmpty())text({680,850,1100,50},shell_.status(),26,QColor(230,230,230));
        // Info bar: the information panel's own material, xmb_plugin_normal:tex_indi_bg, a 24x24 nine-slice
        // tile with an 8 px white alpha ramp on every side around a black interior at alpha 13/255 (5.1%).
        // It frames the clock in the top-right corner; the clock's centre sits at y~104 as in the reference
        // video. A message page keeps only the analog clock face (measured on D4: plate and date text
        // absent, clock kept), which drawPageChrome draws with the page.
        if(!messageScreen) {
            // Measured from the saved 1920x1080 firmware frames (audit/ITEM-MENU-INFO-FIRMWARE.md B3/E4):
            // the visible plate is 591x58 at (1329,75), flush to the right screen edge. The tile's own 1 px
            // transparent margin lies just outside that core (x1328/y74) and its right border falls past
            // x1919, which is why the firmware plate shows top, bottom and left ramps but no right edge.
            // The 9-slice keeps the 8 px border at its native pixel size, so the measured ramp is
            // reproduced instead of being smeared by stretching the whole tile.
            const QRectF infoBar(1329,75,591,58);
            const QImage& plate=assets_.image("xmb_plugin_normal","tex_indi_bg");
            if(!plate.isNull()) {
                drawNineSlice(p,"xmb_plugin_normal","tex_indi_bg",infoBar.adjusted(-1,-1,8,1),8.,1.);
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
            const auto time=shell_.clockDateTime().time();const QPointF center(1791,104);
            const auto hand=[&](double angle,double length){const double a=angle*std::numbers::pi/180.;return QPointF(std::sin(a)*length,-std::cos(a)*length);};
            p.save();p.setBrush(Qt::NoBrush);
            for(int shadow=1;shadow>=0;--shadow) {
                const auto c=center+QPointF(shadow*1.5,shadow*1.5);p.setPen(QPen(shadow?QColor(0,0,0,150):QColor(255,255,255),2.7,Qt::SolidLine,Qt::RoundCap));
                p.drawEllipse(c,14,14);p.drawLine(c,c+hand(time.minute()*6,10));p.drawLine(c,c+hand((time.hour()%12)*30+time.minute()*.5,7));
            }
            p.restore();
            // The firmware's indicator row (page_xmb_indicator in xmb_plugin_normal.rco, B1): five 32x32
            // slots at fixed offsets from the clock, inside the same plate. The whole group is authored at
            // alpha 0 and only the slots the console has state for appear. This build has one real source
            // among the five: Steam friends' presence, for tex_indi_online and its online_num counter.
            // There is no unread-message source (tex_indi_message), no in-XMB text-chat session
            // (tex_indi_chat), no signed-in user avatar (the avatar slot has no RCO image, and this build
            // has no console/PSN account), and no PlayStation Plus membership (tex_indi_plus), so those
            // four stay undrawn: an empty slot is honest, a fabricated count is not.
            int onlineFriends=0;
            for(const auto& category:shell_.categories()) {
                if(category.id!="friends")continue;
                for(const auto& entry:category.entries)
                    if(entry.friendRow && entry.presenceState!=0)++onlineFriends; // persona state 0 = Offline
                break;
            }
            if(onlineFriends>0) {
                // Offsets are the RCO's own -244 message, -274 chat, -342 online, -376 avatar, -410 plus,
                // measured from the clock text's right edge; the 32 px row is centred on the plate (y 104).
                const double iconAnchorX=1760.; // measured clock ink right edge (B3/E4)
                const QRectF online(iconAnchorX-342.,88,32,32);
                p.drawImage(online,assets_.image("xmb_plugin_normal","tex_indi_online"));
                // online_num sits at +14/+2 inside the slot. Its RCO size 10 has no measured px mapping
                // (the clock text is also size 10 but renders a 22 px cap), so 15 px is a fit that keeps
                // the count inside the 32 px slot.
                text(QRectF(online.left()+14,online.top()+2,18,18),QString::number(onlineFriends),15,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            }
        }
    }
    // Message surfaces (dialogs.cpp): the kind decides the shape. An inline line is drawn on the normal
    // frame; the other four own the screen. A box keeps drawing while its traced 200/1000-unit exit fade
    // runs after Shell has dismissed it.
    if(messageUp)drawMessageSurface(p);
    else if(messageShown_.kind==MessageKind::Box && (dialogEase_.value>.002 || dialogPlateEase_.value>.002))drawMessageBox(p);
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
        if(room && hud.visible && hud.opacity>.002) drawVolumeHud(p,hud.opacity);
    }
    // The console's notification pop-up, drawn last so it sits above the XMB, and never while the PS menu is
    // up: that menu blacks the frame and owns the screen. It takes no input (nothing here reads keys), so it
    // cannot steal a press from whatever is underneath.
    if(!shell_.psMenuOpen() && notificationEase_.value>.002 && !notificationShown_.title.isEmpty())
        drawNotification(p,notificationEase_.value);
    // PS button: the console's system menu (RPCS3 capture 2026-10-26): the frame goes black except the clock,
    // a centred two-item menu, the controller/charge panel bottom left and the Enter/Back legend. A hold opens
    // it; "Turn Off the System" asks once before quitting.
    if(shell_.psMenuOpen()) {
        // The console's system menu (RPCS3 capture, audit/PS-BUTTON.md): black frame, clock still top right,
        // the menu centred, the controller/charge panel bottom left, Cross/O legend along the bottom.
        p.save();
        p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0));   // the console blacks the whole frame
        text({1500,95,420,45},shell_.clockText(),27,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
        const auto psItems=shell_.psMenuItems();
        const double psFirst=psItems.size()>1?500.:520.;
        for(int i=0;i<psItems.size();++i) {
            const bool focused=!shell_.psMenuConfirmingOff() && i==shell_.psMenuSelection();
            const QString label=shell_.psMenuConfirmingOff() && i==0?QObject::tr("Turn Off the System?"):psItems[i];
            text({360,psFirst+i*40.,1200,34},label,22,focused?Qt::white:QColor(160,160,160),Qt::AlignHCenter|Qt::AlignVCenter);
        }
        if(shell_.psMenuConfirmingOff())
            text({360,psFirst+80,1200,34},QObject::tr("X turns the XMB off; the PC stays on   O cancels"),20,QColor(150,150,150),Qt::AlignHCenter|Qt::AlignVCenter);
        for(int i=0;i<4;++i) {
            const QRectF slot(157+i*26.,682,20,20);
            p.fillRect(slot,i==0?QColor(214,72,72):QColor(68,68,68));
            text(slot,QString::number(i+1),14,QColor(235,235,235),Qt::AlignCenter);
        }
        text({260,675,140,32},QObject::tr("Controller"),20,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
        text({400,675,220,32},"1",20,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
        text({260,707,140,32},QObject::tr("Charge Level"),20,QColor(215,215,215),Qt::AlignLeft|Qt::AlignVCenter);
        text({400,707,220,32},"-",20,QColor(215,215,215),Qt::AlignLeft|Qt::AlignVCenter);
        drawImage(p,"system_plugin","tex_cross",{856,858,26,26});text({890,850,140,40},QObject::tr("Enter"),21,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
        drawImage(p,"system_plugin","tex_circle",{1010,858,26,26});text({1044,850,140,40},QObject::tr("Back"),21,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
        p.restore();
    }
    else if(psHudUntil_>0 && clock_.elapsed()<psHudUntil_) {
        // A PS tap flashes the controller indicator beside the clock, then fades.
        const double fade=std::clamp(double(psHudUntil_-clock_.elapsed())/600.,0.,1.);
        p.save();p.setOpacity(fade);
        drawImage(p,"system_plugin","tex_controller",{1230,92,34,34});
        text({1270,88,150,42},"1",22,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
        text({1430,88,90,42},"-",22,QColor(210,210,210),Qt::AlignRight|Qt::AlignVCenter);
        p.restore();
    }
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
void View::pollVolumeRamp(float down,float up,qint64 now) {
    // The pad's triggers are analog axes, not D-pad bits: nothing here touches `buttons`, `repeatAt_` or the
    // navigation paths, so a held trigger cannot move the XMB or consume a D-pad repeat.
    constexpr float kPress=.35f;      // a resting DualShock 3/4 trigger reads 0.0; this is the press threshold
    if(volumeInputBlocked()){volumeRamp_=0;return;}
    const float pressure=std::max(down,up);
    if(pressure<kPress) {
        if(volumeRamp_ && volumeTrace())qInfo()<<"volume: trace ramp released";
        volumeRamp_=0;
        return;
    }
    const int direction=up>down?1:-1;
    const qint64 interval=pressure>=.75f?60:120; // full squeeze ramps twice as fast
    if(direction!=volumeRamp_) {                  // the press: one step now, then the held repeat
        volumeRamp_=direction;volumeRampAt_=now+interval;
        if(volumeTrace())qInfo().noquote()<<QString("volume: trace ramp start %1 pressure %2 interval %3 ms")
            .arg(direction>0?"R2":"L2").arg(pressure,0,'f',2).arg(interval);
        volumeStep(direction,direction>0?QStringLiteral("R2"):QStringLiteral("L2"));
        return;
    }
    if(now<volumeRampAt_)return;
    // Counted from the last deadline, not from this poll, so the 16 ms poll cannot stretch the interval.
    volumeRampAt_=std::max(volumeRampAt_,now-interval)+interval;
    volumeStep(direction,direction>0?QStringLiteral("R2"):QStringLiteral("L2"));
}
// The native volume HUD. Its shape comes from the firmware: the console's volume display is one page holding
// a single horizontal level meter (videoplayer_plugin.rco videoplayer_volume_page /
// videoplayer_volume_level_meter, and the same pair in audioplayer_plugin.rco and explore_plugin_full.rco
// volume_page / volume_level_meter). That page's projection lives in the plugin's own paf code and is not
// recovered, so the rect here is fitted rather than traced - and it is fitted to the one band no other
// composite uses: immediately left of the information panel (x 1329..1920, y 75..133), above the
// notification plaque (y 145), left of the options panel (x 1330) and clear of the OSK (y 290) and the PS
// menu (which blacks the frame). The backing is the information panel's own material,
// xmb_plugin_normal:tex_indi_bg (the same 24x24 nine-slice tile the info bar uses), so the HUD reads as part
// of the XMB rather than as an application overlay; its rect stays a FIT, not a traced projection.
void View::drawVolumeHud(QPainter& p,double opacity) {
    const double alpha=std::clamp(opacity,0.,1.);
    if(alpha<=.002)return;
    const QRectF panel(1000,75,222,58);
    p.save();
    p.setOpacity(alpha);
    const QImage& plate=assets_.image("xmb_plugin_normal","tex_indi_bg");
    if(plate.isNull()) {
        // Keep the pre-nine-slice drawing if the tile cannot be read (audit/TEXTURE-COVERAGE.md: it does).
        static bool hudFallbackWarned=false;
        if(!hudFallbackWarned){hudFallbackWarned=true;qWarning().noquote()<<"view: volume HUD plate xmb_plugin_normal/tex_indi_bg did not decode; drawing the tex_infobar fallback";}
        p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,20));p.drawRoundedRect(panel,4,4);
        p.setOpacity(.12*alpha);p.drawImage(panel,assets_.image("system_plugin","tex_infobar"));
        p.setOpacity(alpha);p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(255,255,255,90),1));
        p.drawRoundedRect(panel.adjusted(.5,.5,-.5,-.5),4,4);
    } else drawNineSlice(p,"xmb_plugin_normal","tex_indi_bg",panel,8.,alpha);
    const auto label=[&](const QRectF& rect,const QString& value,int size,QColor colour,int flags) {
        if(textRenderer_.ready()) {
            QFont font(shell_.fontFamily());font.setPixelSize(size);
            textRenderer_.draw(*this,p,rect,value,font,colour,flags);return;
        }
        QFont font(shell_.fontFamily());font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
        p.setPen(QColor(0,0,0,38));p.drawText(rect.translated(1,1),flags,value);
        p.setPen(colour);p.drawText(rect,flags,value);
    };
    // The firmware's own label for this display (msg_volume_control, "Volume Control" in the player plugins).
    label(QRectF(panel.left()+12,panel.top()+7,150,18),
        assets_.text("videoplayer_plugin","msg_volume_control","Volume Control"),15,QColor(220,220,220),Qt::AlignLeft|Qt::AlignVCenter);
    const int level=std::clamp(volumeLevel_,0,100);
    // Muted: the level is still what it is, but nothing is audible, so the number gives way to "Mute" and the
    // meter greys. Nothing here writes the mute state except the mute key itself.
    label(QRectF(panel.right()-122,panel.top()+5,110,24),
        volumeMuted_?QStringLiteral("Mute"):QStringLiteral("%1%").arg(level),19,
        volumeMuted_?QColor(200,200,200):Qt::white,Qt::AlignRight|Qt::AlignVCenter);
    const QRectF track(panel.left()+12,panel.top()+37,panel.width()-24,6);
    p.setPen(Qt::NoPen);p.setBrush(QColor(255,255,255,55));p.drawRect(track);
    if(level>0)p.fillRect(QRectF(track.left(),track.top(),track.width()*level/100.,track.height()),
        volumeMuted_?QColor(150,150,150):QColor(255,255,255));
    p.restore();
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
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier);keyPressEvent(&press);
    QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier);keyReleaseEvent(&release);
}
// Scripted PS tokens feed the global watcher exactly like a HID press: the tap keeps the button down
// briefly, the hold long enough for the watcher's 1 s rule to fire on its own.
void View::replayPsTap() {
    psLog("script: token ps (tap)");
    psWatcher_.replayPress();
    QTimer::singleShot(120,this,[this]{psWatcher_.replayRelease();});
}
void View::replayPsHold() {
    psLog("script: token pshold (hold)");
    psWatcher_.replayPress();
    QTimer::singleShot(1200,this,[this]{psWatcher_.replayRelease();});
}
bool View::titleRunning() const {
    // The only public, side-effect-free window onto Shell's tracked-title state (shell.h is owned by
    // another change): psMenuItems() adds Quit Game exactly when Shell::gameRunning() is true, for a
    // tracked QProcess title or a Steam handoff. The console menu is two rows otherwise.
    return shell_.psMenuItems().size()>2;
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
    if(running)bringXmbForward("PS hold while a tracked title is running");
    if(shell_.psMenuOpen()){psLog("event: system menu already open");return;}
    shell_.psHeld();
    psLog(QString("event: system menu opened rows=%1 quitGameOffered=%2 foreground=%3")
        .arg(shell_.psMenuItems().size()).arg(running?1:0).arg(foregroundDescription()));
    update();
}
void View::psTappedEvent(qint64 durationMs) {
    const bool running=titleRunning();
    psLog(QString("event: tap %1 ms xmbActive=%2 xmbForeground=%3 titleRunning=%4 foreground=%5")
        .arg(durationMs).arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(running?1:0).arg(foregroundDescription()));
    if(!psPressActionable_) { psLog("event: tap ignored (not actionable)"); return; }
    if(QApplication::activeModalWidget()) { psLog("event: tap ignored (a modal dialog owns input)"); return; }
    if(!running && !xmbOwnsForeground()) { psLog("event: tap ignored (XMB lost the foreground before the tap completed)"); return; }
    if(running)bringXmbForward("PS tap while a tracked title is running");
    psHudUntil_=clock_.elapsed()+2500;
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
    if(quitting) {
        psLog(QString("event: Quit Game confirmed; tracked title terminated=%1").arg(titleRunning()?0:1));
        reassertForeground("Quit Game terminated the tracked title");
    }
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
    // The PS system menu owns the keyboard while it is open: arrows move, Return confirms, Escape backs out.
    if(shell_.psMenuOpen()) {
        if(e->key()==Qt::Key_Up)shell_.psMenuMove(-1);
        else if(e->key()==Qt::Key_Down)shell_.psMenuMove(1);
        else if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter)activatePsMenuSelection();
        else if(e->key()==Qt::Key_Escape)shell_.psMenuClose();
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
    if(shell_.photoOpen() && shell_.message().isEmpty()) {
        switch(e->key()) {
        case Qt::Key_Plus:case Qt::Key_Equal:zoomPhoto(1);e->accept();return;
        case Qt::Key_Minus:zoomPhoto(-1);e->accept();return;
        case Qt::Key_0:resetPhotoView();e->accept();return;
        case Qt::Key_Q:shell_.vertical(-1);e->accept();return;
        case Qt::Key_E:shell_.vertical(1);e->accept();return;
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
    case Qt::Key_Escape:case Qt::Key_Backspace:shell_.back();break;
    case Qt::Key_Space:shell_.togglePlayback();break;
    case Qt::Key_BracketLeft:shell_.skipMedia(-1);break;
    case Qt::Key_BracketRight:shell_.skipMedia(1);break;
    case Qt::Key_M:shell_.toggleMediaShuffle();break;
    case Qt::Key_R:shell_.cycleMediaRepeat();break;
    case Qt::Key_PageUp:shell_.vertical(-10);break;case Qt::Key_PageDown:shell_.vertical(10);break;
    case Qt::Key_F5:shell_.refreshDirectory();break;
    // Triangle on the XMB: the selected entry's options (keyboard substitute; controllers send Y).
    case Qt::Key_T:shell_.toggleOptions();break;
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
    if(!xmbOwnsForeground() || QApplication::activeModalWidget()){buttons_=keyHeld_=keyPressed_=0;volumeRamp_=0;return;}
    // A --script run is the script's own input: every scripted key goes through replayKeyTap, which dispatches the
    // same handlers the pad drives. A physical controller is therefore not read at all, so a capture cannot depend
    // on a pad lying on its d-pad (which used to walk the keyboard focus off the default key between two grabs).
    static const bool scriptedInput=QCoreApplication::arguments().contains(QStringLiteral("--script"));
    // XInput pad 0 and any PlayStation controller over HID (pad.cpp), merged; the left stick doubles as the D-pad.
    XINPUT_STATE state{};const bool xinput=!scriptedInput && XInputGetState(0,&state)==ERROR_SUCCESS;
    pad::State hid;const bool playstation=!scriptedInput && pad::latest(hid);
    quint16 buttons=quint16((xinput?state.Gamepad.wButtons:0)|(playstation?hid.buttons:0)|keyHeld_);
    // PS is consumed by the global watcher (ps_watcher.cpp), which also covers a focused game; it must
    // never reach the XMB navigation bits below.
    buttons=quint16(buttons&~0x400);
    // L2/R2 are analog triggers rather than D-pad bits: they are read here and never merged into `buttons`,
    // so a held trigger cannot move the XMB, cannot press Cross, and cannot disturb repeatAt_. Both the pad's
    // own report (pad.cpp l2/r2) and an XInput pad's triggers feed the same ramp; whichever is pressed harder
    // wins when both controllers are attached.
    const float triggerDown=std::max(xinput?state.Gamepad.bLeftTrigger/255.f:0.f,playstation?hid.l2:0.f);
    const float triggerUp=std::max(xinput?state.Gamepad.bRightTrigger/255.f:0.f,playstation?hid.r2:0.f);
    pollVolumeRamp(triggerDown,triggerUp,clock_.elapsed());
    if(shell_.psMenuOpen()) {
        const auto edgePs=quint16((buttons&~buttons_)|keyPressed_);keyPressed_=0;buttons_=buttons;
        if(edgePs&XINPUT_GAMEPAD_DPAD_UP)shell_.psMenuMove(-1);
        if(edgePs&XINPUT_GAMEPAD_DPAD_DOWN)shell_.psMenuMove(1);
        if(edgePs&XINPUT_GAMEPAD_A)activatePsMenuSelection();
        if(edgePs&XINPUT_GAMEPAD_B)shell_.psMenuClose();
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
    if(xinput || playstation) {
        const float lx=xinput?state.Gamepad.sThumbLX/32767.f:hid.lx,ly=xinput?-state.Gamepad.sThumbLY/32767.f:hid.ly; // +y down, as HID
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
    if(shell_.photoOpen() && shell_.message().isEmpty()) {
        if(photoViewport_.canPan()) {
            if(movement&XINPUT_GAMEPAD_DPAD_LEFT)panPhoto(-1,0);if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)panPhoto(1,0);
            if(movement&XINPUT_GAMEPAD_DPAD_UP)panPhoto(0,-1);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)panPhoto(0,1);
        } else {
            if(movement&XINPUT_GAMEPAD_DPAD_LEFT)shell_.horizontal(-1);if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)shell_.horizontal(1);
            if(movement&XINPUT_GAMEPAD_DPAD_UP)shell_.vertical(-1);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)shell_.vertical(1);
        }
        if(edge&XINPUT_GAMEPAD_LEFT_SHOULDER)zoomPhoto(-1);if(edge&XINPUT_GAMEPAD_RIGHT_SHOULDER)zoomPhoto(1);
        if(edge&XINPUT_GAMEPAD_X)resetPhotoView();if(edge&XINPUT_GAMEPAD_Y)shell_.vertical(1);
        if(edge&XINPUT_GAMEPAD_A)shell_.activate();if(edge&XINPUT_GAMEPAD_B)shell_.back();
        if(edge&0x000f)repeatAt_=now+repeatDelay;else if(movement&0x000f)repeatAt_=nextRepeat;
        buttons_=buttons;return;
    }
    if(movement&XINPUT_GAMEPAD_DPAD_LEFT)shell_.horizontal(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)shell_.horizontal(1);
    if(movement&XINPUT_GAMEPAD_DPAD_UP)shell_.vertical(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_DOWN)shell_.vertical(1);
    if(edge&XINPUT_GAMEPAD_A)shell_.activate();if(edge&XINPUT_GAMEPAD_B)shell_.back();
    // Triangle: the item options panel on a content row, and the in-player panel (subtitles, audio
    // tracks, repeat, shuffle) while a video plays. Music, which has no player panel, keeps Triangle as
    // Repeat, and Repeat also keeps the R key, so nothing lost a binding it had.
    if(edge&XINPUT_GAMEPAD_Y) {
        if(!shell_.mediaOpen() || shell_.videoOpen())shell_.toggleOptions();
        else shell_.cycleMediaRepeat();
    }
    if(shell_.mediaOpen()) {
        if(edge&XINPUT_GAMEPAD_LEFT_SHOULDER)shell_.skipMedia(-1);
        if(edge&XINPUT_GAMEPAD_RIGHT_SHOULDER)shell_.skipMedia(1);
        if(edge&XINPUT_GAMEPAD_X)shell_.toggleMediaShuffle();
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
    // The caret is two marks, not one (5.2): a 2 px I-beam with 12 px serifs, 27 px tall, at the pen advance, in the
    // measured 130,249,12 green, plus the hollow triangle 12 px to its right. Both are drawn only while the blink
    // phase is lit; the phase comes from OskCaret::blinkMilliseconds in paintGL.
    if(osk_.caretLit) {
        const double x=textX+before-scroll;
        const double y=l.bands.field.top()+9;
        p.fillRect(QRectF(x,y,l.caret.shaftWidth,l.caret.height),l.caret.colour);
        const double serifX=x-(l.caret.serifWidth-l.caret.shaftWidth)/2.;
        p.fillRect(QRectF(serifX,y,l.caret.serifWidth,2),l.caret.colour);
        p.fillRect(QRectF(serifX,y+l.caret.height-2,l.caret.serifWidth,2),l.caret.colour);
        const double markerX=x+l.caret.markerInset;
        const double markerTop=y+3;
        p.setPen(QPen(Qt::white,2));p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(markerX,markerTop),QPointF(markerX,markerTop+l.caret.markerHeight));
        p.drawLine(QPointF(markerX,markerTop),QPointF(markerX+l.caret.markerWidth,markerTop+l.caret.markerHeight/2.));
        p.drawLine(QPointF(markerX,markerTop+l.caret.markerHeight),
                   QPointF(markerX+l.caret.markerWidth,markerTop+l.caret.markerHeight/2.));
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
