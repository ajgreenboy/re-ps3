#include "view.h"
#include <QPainter>
#include <QKeyEvent>
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
#include <QTextStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTime>
#include <cmath>
#include <numbers>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#endif

namespace xmb {
View::View(Shell& shell,Assets& assets,QWidget* parent):QOpenGLWidget(parent),shell_(shell),assets_(assets) {
    setFocusPolicy(Qt::StrongFocus);setMinimumSize(800,450);
    // Decode and cache frequently used original assets before animation starts.
    for(const auto& category:shell_.categories()) {
        assets_.image(category.container,category.texture);
        for(const auto& entry:category.entries)assets_.image(entry.container,entry.texture);
    }
    syncColumns();
    monthImage_=assets_.image("qgl/lines",QString("textures/month_bg/rgb/%1.dds").arg(QDateTime::currentDateTime().date().month(),2,10,QChar('0')));
    connect(&shell_,&Shell::changed,this,[this]{syncColumns();loadWallpaper();update();});
    connect(&shell_,&Shell::mediaFrame,this,[this](const QImage& frame){mediaFrame_=frame;update();});
    connect(&shell_,&Shell::requestLibrary,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Choose media folder");if(!path.isEmpty())shell_.chooseLibrary(path);});
    connect(&shell_,&Shell::requestWallpaper,this,[this]{const auto file=QFileDialog::getOpenFileName(this,"Choose Wallpaper",{},"Images (*.png *.jpg *.jpeg *.bmp *.webp)");if(!file.isEmpty())shell_.chooseWallpaper(file);});
    connect(&shell_,&Shell::requestSystemName,this,[this](const QString& value){openOsk("System Name",value,[this](const QString& name){shell_.setSystemName(name);});});
    connect(&shell_,&Shell::requestNewUser,this,[this]{openOsk("Create New User",{},[this](const QString& name){shell_.createUser(name);});});
    loadWallpaper();
    clock_.start();timer_.setTimerType(Qt::PreciseTimer);timer_.setInterval(16);
    connect(&timer_,&QTimer::timeout,this,[this]{pollController();update();});timer_.start();
}
void View::syncColumns() {
    const int depth=shell_.depth();
    if(previousCategory_!=shell_.category()) {
        particles_.nudge(shell_.category()>previousCategory_?1.f:-1.f); // PARTICLES_UI: the dust reacts to navigation
        outgoing_=columns_;outgoingCategory_=previousCategory_;categoryFade_=1.;columns_.clear();
        previousCategory_=shell_.category();
    }
    // Keep the departed child until the return animation finishes. The shell's
    // actual page stack and selections change immediately and remain canonical.
    for(int i=0;i<=depth;++i) {
        if(i>=columns_.size())columns_.push_back(Column{shell_.entriesAt(i),shell_.selectionAt(i),double(shell_.selectionAt(i))});
        else {columns_[i].entries=shell_.entriesAt(i);columns_[i].selection=shell_.selectionAt(i);}
    }
    if(depth>previousDepth_)for(int i=previousDepth_+1;i<=depth;++i)columns_[i].position=columns_[i].selection;
    previousDepth_=depth;
}
void View::loadWallpaper() {
    if(shell_.backgroundMode()!=2)return;
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
View::~View(){if(context()){makeCurrent();monthTexture_.reset();if(vao_)glDeleteVertexArrays(1,&vao_);background_.removeAllShaders();doneCurrent();}}
void View::initializeGL() {
    if(!initializeOpenGLFunctions()){qCritical()<<"Native renderer requires OpenGL 3.3";return;}
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
    particles_.init(*this,assets_);
    if(!monthImage_.isNull()) {
        monthTexture_=std::make_unique<QOpenGLTexture>(monthImage_.mirrored(false,true));
        monthTexture_->setMinificationFilter(QOpenGLTexture::Linear);monthTexture_->setMagnificationFilter(QOpenGLTexture::Linear);
        monthTexture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    qInfo()<<"native renderer:"<<reinterpret_cast<const char*>(glGetString(GL_RENDERER));
}
// Background brightness by clock time, 0 = night, 1 = day. Keyframes are the top-band brightness of
// the reference day-cycle video relative to 10:30-16:30 (audit/VIDEO-REFERENCES.md); linear between them.
double View::dayLight(double hour) {
    static const double keys[][2]={{0,0},{3,0},{4.5,.38},{6,.77},{7.5,.93},{9,.98},{10.5,1},{16.5,1},{18,.82},{19.5,.24},{21,0},{24,0}};
    hour=std::fmod(std::fmod(hour,24.)+24.,24.);
    for(size_t i=1;i<std::size(keys);++i)
        if(hour<=keys[i][0])return keys[i-1][1]+(keys[i][1]-keys[i-1][1])*(hour-keys[i-1][0])/(keys[i][0]-keys[i-1][0]);
    return 0;
}
void View::renderBackground(float seconds,float light,int pixelWidth,int pixelHeight) {
    static const QVector3D colours[]={{.55f,.39f,.13f},{.12f,.36f,.68f},{.40f,.20f,.60f},{.62f,.17f,.18f},{.16f,.43f,.32f},{.48f,.50f,.54f}};
    // Original colour theme: the firmware's own background shader and monthly palettes.
    // The firmware background renders every colour setting (as on the PS3, a fixed colour is the same wave pinned to one
    // palette); only the wallpaper mode bypasses it.
    const bool original=originalReady_ && (forceOriginal_ || shell_.backgroundMode()!=2);
    const bool originalRibbon=original && ribbon_.ready();
    const bool originalParticles=originalRibbon && particles_.ready() && shell_.backgroundMode()!=1; // "classic" hides particles
    GLint target=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);
    const bool hdr=original && hdr_.ready(); // the original background is drawn as HDR and tone mapped with glare
    float presetBlend=1; // day/night preset weight for ribbon, particles and HDR: 0 night .. 1 day
    if(original) {
        const GLuint scene=hdr?hdr_.begin(*this,pixelWidth,pixelHeight):GLuint(target);
        QDateTime when=backgroundOverride_.isValid()?backgroundOverride_:QDateTime::currentDateTime();
        // Fixed colours pin the month palette (day 1: that month's textures alone); the clock still drives day/night.
        static const int fixedMonth[]={0,8,6,12,5,1}; // Original, Blue, Purple, Red, Green, Silver
        if(!backgroundOverride_.isValid() && shell_.theme()>0 && shell_.theme()<int(std::size(fixedMonth)))
            when=QDateTime(QDate(when.date().year(),fixedMonth[shell_.theme()],1),when.time());
        if(when.date().month()!=texturesMonth_)loadMonthTextures(when.date().month());
        const BackgroundTime time=backgroundTime(when);presetBlend=std::clamp((time.nightDayBlend-.5f)/.5f,0.f,1.f); // NightDayBlend runs 0.5 (night) .. 1
        renderOriginalBackground(time,pixelWidth,pixelHeight);
        if(originalRibbon)ribbon_.render(*this,seconds,presetBlend,pixelWidth,pixelHeight,scene);
        if(originalParticles) {
            particles_.update(seconds,ribbon_.centreLine());
            particles_.render(*this,presetBlend,pixelWidth,pixelHeight);
        }
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
    if(hdr)hdr_.finish(*this,presetBlend,GLuint(target),originalParticles?std::function<void(int,int)>([&](int w,int h){particles_.render(*this,presetBlend,w,h,true);}):std::function<void(int,int)>());
}
// Inputs of qgl/lines back_colours0.fpo, as custom_render_plugin's setter (FUN_00052ad8) derives them from the clock
// as a day fraction t in [0,1] and the month as a float in [0,12] (constants at 0x92e54..0x92e6c, preset hours / 24
// at 0x91f6c), with the override/day BACKGROUND.mnu values.
View::BackgroundTime View::backgroundTime(const QDateTime& when) {
    const auto clock=when.time();const double hour=clock.hour()+clock.minute()/60.+clock.second()/3600.,t=hour/24.;
    const auto date=when.date();
    BackgroundTime b;
    // _DayTime: a day-curve of t (FUN_00052a28, whose arguments the decompile lost) x 2400. Only the day half of the
    // shader reads it (dark below ~400 and above ~2000, peak at 1200). Fitted: DAYSPREAD (2.68038) widens 06:00-18:00
    // around noon (16:30 still full, 18:00 ~80% in the reference); outside that the curve falls to 0 / 2400 at
    // midnight through keyframes fitted to the reference (03:00 black, 04:30 ~40%, 19:30 ~25%, 21:00 black).
    constexpr double daySpread=2.68038;
    const auto spread=[&](double h){return 1200.+(h-12.)*100./daySpread;};
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
    constexpr double nightBlend=.499947;
    const double s1=smooth(0./24,5.16611/24,t),s2=smooth(18.498/24,20.3312/24,t);
    b.nightDayBlend=float(1.+nightBlend*(s1*(1.-s2)-1.));
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
    if(!original_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) || !original_.addShaderFromSourceCode(QOpenGLShader::Fragment,backColours_.glsl) || !original_.link()) {
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
    out<<"# ease-out: settle to 10% = time constant * ln(10)\n";
    for(const auto& [name,tau]:std::initializer_list<std::pair<const char*,double>>{{"category",.120},{"item",.085},{"depth/entry",.110}})
        out<<name<<": tau "<<int(tau*1000)<<" ms, settle "<<int(tau*std::log(10.)*1000)<<" ms ("<<QString::number(tau*std::log(10.)*60,'f',1)<<" frames at 60 fps)\n";
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
    const qint64 now=clock_.elapsed();
    const double dt=std::min(.1,double(now-previous_)/1000.);previous_=now;
    // Ease-out approach rates from the 60 fps reference (audit/VIDEO-REFERENCES.md): category moves
    // settle in ~180-280 ms, item moves in ~150-250 ms. Exponential time constants in seconds.
    const double categoryBlend=1.-std::exp(-dt/.120),itemBlend=1.-std::exp(-dt/.085),depthBlend=1.-std::exp(-dt/.110);
    categoryPosition_+=(shell_.category()-categoryPosition_)*categoryBlend;
    categoryFade_*=std::exp(-dt/.120);if(categoryFade_<.001){categoryFade_=0;outgoing_.clear();}
    depthPosition_+=(shell_.depth()-depthPosition_)*depthBlend;
    for(auto& column:columns_)column.position+=(column.selection-column.position)*itemBlend;
    if(std::abs(depthPosition_-shell_.depth())<.001) {
        depthPosition_=shell_.depth();
        if(columns_.size()>shell_.depth()+1)columns_.resize(shell_.depth()+1);
    }
    glClearColor(.04f,.04f,.05f,1.f);glClear(GL_COLOR_BUFFER_BIT);
    if(glReady_) {
        const auto time=QTime::currentTime();
        renderBackground(float(now)/1000.f,float(dayLight(time.hour()+time.minute()/60.+time.second()/3600.)),
            int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));
    }
    // The UI layer is drawn by Qt's raster engine at the window's real pixel size (antialiased, properly rasterised
    // glyphs at any scale) and composited over the GL background in one draw. The GL paint engine renders scaled text
    // unhinted and jagged.
    const QSize pixels(int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));
    if(uiLayer_.size()!=pixels)uiLayer_=QImage(pixels,QImage::Format_ARGB32_Premultiplied);
    uiLayer_.fill(Qt::transparent);
    {
        QPainter ui(&uiLayer_);
        ui.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
        const double scale=std::min(pixels.width()/1920.,pixels.height()/1080.);
        ui.translate((pixels.width()-1920*scale)/2,(pixels.height()-1080*scale)/2);ui.scale(scale,scale);drawScene(ui);
    }
    QPainter painter(this);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(0,0,width(),height()),uiLayer_);
    painter.end();
    ++frames_;if(now-reportAt_>=5000){qInfo()<<"native frames:"<<frames_*1000./double(now-reportAt_)<<"fps; decoded images"<<assets_.decodedImages()<<"missing images"<<assets_.failedImages();frames_=0;reportAt_=now;}
}
void View::drawImage(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal opacity) {
    if(container=="qgl/icontex") {
        p.save();p.setOpacity(opacity);drawCategory(p,Category{texture,{},container,texture,{}},rect);p.restore();return;
    }
    const auto& image=assets_.image(container,texture);
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
void View::drawCategory(QPainter& p,const Category& category,const QRectF& rect) {
    const auto& original=assets_.image(category.container,category.texture);
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
void View::drawScene(QPainter& p) {
    QFont font(shell_.fontFamily());font.setPixelSize(27);p.setFont(font);
    auto text=[&](const QRectF& r,const QString& value,int size,QColor colour,int flags=Qt::AlignLeft|Qt::AlignVCenter){
        QFont f(shell_.fontFamily());f.setPixelSize(size);f.setStyleStrategy(QFont::PreferAntialias);f.setHintingPreference(QFont::PreferVerticalHinting);p.setFont(f);p.setPen(QColor(0,0,0,38));p.drawText(r.translated(1,1),flags,value);p.setPen(colour);p.drawText(r,flags,value);
    };
    // Soft white halo behind a label (the XMB's focused-item glow): the text drawn at low alpha in a ring of offsets.
    auto glow=[&](const QRectF& r,const QString& value,int size,double strength,int flags=Qt::AlignLeft|Qt::AlignVCenter){
        QFont f(shell_.fontFamily());f.setPixelSize(size);f.setStyleStrategy(QFont::PreferAntialias);p.setFont(f);
        // Reference (3x crop, 64 s video): a luminous halo ~5 px wide around the focused label.
        for(const auto& [radius,alpha]:{std::pair{5.,7},std::pair{2.5,16}})for(int k=0;k<12;++k) {
            const double a=k*std::numbers::pi/6;
            p.setPen(QColor(235,245,255,int(alpha*strength)));p.drawText(r.translated(std::cos(a)*radius,std::sin(a)*radius),flags,value);
        }
    };
    if(shell_.backgroundMode()==2 && !wallpaperImage_.isNull()) {
        const auto size=QSizeF(wallpaperImage_.size()).scaled(QSizeF(1920,1080),Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRectF((1920-size.width())/2,(1080-size.height())/2,size.width(),size.height()),wallpaperImage_);
        p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,35));
    }
    if(shell_.mediaOpen()) {
        if(!mediaFrame_.isNull()) {
            p.fillRect(QRectF(0,0,1920,1080),Qt::black);
            const auto size=QSizeF(mediaFrame_.size()).scaled(QSizeF(1920,1080),shell_.photoOpen() && shell_.photoFill()?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);
            p.drawImage(QRectF(QPointF((1920-size.width())/2,(1080-size.height())/2),size),mediaFrame_);
        }
        text({110,970,1500,50},shell_.mediaTitle(),27,Qt::white);
        text({110,1020,1700,35},shell_.photoOpen()?QString("Circle  Back     Cross  ")+(shell_.slideshowActive()?"Pause Slideshow":"Start Slideshow")+"     Left / Right  Previous / Next     Up / Down  Rotate":"Circle  Back     Cross  Play / Pause     Left / Right  Seek",21,QColor(230,230,230));
        if(shell_.busy())text({110,910,1000,45},"Loading…",26,Qt::white);
        if(!shell_.photoOpen() && shell_.mediaDuration()>0) {
            const double ratio=std::clamp(double(shell_.mediaPosition())/shell_.mediaDuration(),0.,1.);
            p.fillRect(QRectF(110,941,1700,4),QColor(255,255,255,60));p.fillRect(QRectF(110,941,1700*ratio,4),Qt::white);
            const auto time=[](qint64 ms){const auto seconds=ms/1000;return QString("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0'));};
            text({1450,963,360,45},time(shell_.mediaPosition())+" / "+time(shell_.mediaDuration()),23,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
        }
    } else {
        const auto& categories=shell_.categories();
        const double childAmount=std::clamp(depthPosition_,0.,1.);
        for(int i=0;i<categories.size();++i) {
            const auto& cat=categories[i];const double distance=i-categoryPosition_,x=560+distance*202-childAmount*250;
            const double focus=std::max(0.,1.-std::abs(distance));const double size=82+focus*10; // reference: ~86 px, focus barely larger
            p.save();p.setOpacity((i==shell_.category()?1.:1.-childAmount)*(shell_.message().isEmpty()?1.:.18));
            drawCategory(p,cat,{x-size/2,290-size/2,size,size});
            if(i==shell_.category()){p.setOpacity((1.-childAmount)*(shell_.message().isEmpty()?1.:.18));text({x-140,335,280,40},cat.title,21,Qt::white,Qt::AlignCenter);}
            p.restore();
        }
        const auto isChoiceColumn=[](const Column& column) {
            if(column.entries.isEmpty())return false;
            for(const auto& entry:column.entries)if(entry.action!=Action::SetSetting)return false;
            return true;
        };
        // Options panel (reference frame 45.4 s): a translucent light strip from x~1330 to the right edge with a bright
        // left edge; options 23 px, 40 px apart, centred on the focus row; the focused option glows. Slides in from the right.
        const auto drawOptionsPanel=[&](const Column& column,double alpha,double ahead) {
            const double x0=1330+ahead*600;
            p.save();p.setOpacity(alpha);
            QLinearGradient fill(x0,0,1920,0);fill.setColorAt(0,QColor(255,255,255,46));fill.setColorAt(.08,QColor(255,255,255,30));fill.setColorAt(1,QColor(255,255,255,22));
            p.fillRect(QRectF(x0,0,1920-x0,1080),fill);
            QLinearGradient edge(x0-10,0,x0+4,0);edge.setColorAt(0,QColor(255,255,255,0));edge.setColorAt(.75,QColor(255,255,255,70));edge.setColorAt(1,QColor(255,255,255,0));
            p.fillRect(QRectF(x0-10,0,14,1080),edge);
            for(int i=0;i<column.entries.size();++i) {
                const double distance=i-column.position,y=505+distance*40;
                if(y<-40 || y>1120)continue;
                const double focus=std::max(0.,1.-std::abs(distance));
                const QRectF rect(x0+16,y-20,1920-x0-40,40);
                if(focus>.5)glow(rect,column.entries[i].title,23,(focus-.5)*2);
                text(rect,column.entries[i].title,23,QColor(255,255,255,int(255*(.78+.22*focus))));
            }
            p.restore();
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
                if(columnAlpha>0)drawOptionsPanel(column,columnAlpha,ahead);
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
                    -std::clamp(-distance,0.,1.)*(level==0?300:95);
                const double y=505+row;
                if(y<-100 || y>1050)continue;
                const double focus=std::max(0.,1.-std::abs(distance));
                const double iconX=560-behind*(100+150*focus)+ahead*250+offset;
                // Reference (RPCS3 4.x video): square icons ~60 px, ~105 px when focused; utility folders are icons too.
                // Only media thumbnails (album art) keep a wide tile.
                const double size=level==0?60+focus*44:40+focus*16;
                const bool tile=entry.texture.contains("album") || entry.container=="file"; // ICON0 thumbnails are wide
                const double iconWidth=tile?155+focus*165:size,iconHeight=tile?90+focus*90:size;
                const double iconAlpha=columnAlpha*(1.-behind*.78*(1.-focus));
                drawImage(p,entry.container,entry.texture,{iconX-iconWidth/2,y-iconHeight/2,iconWidth,iconHeight},iconAlpha);
                p.save();p.setOpacity(columnAlpha*(1.-behind)*(1.-panelAmount*(1.-focus)));
                // One label column per level, as on the PS3 (reference: x ~669 at the top level, ~624 inside a page).
                const double labelX=(level==0?(tile?744:670):624)+ahead*250+offset;
                const double alpha=.72+.28*focus;
                const auto value=active?shell_.valueText(entry):QString{};
                QFont labelFont(shell_.fontFamily());labelFont.setPixelSize(27);
                const auto label=QFontMetrics(labelFont).elidedText(entry.title,Qt::ElideRight,value.isEmpty()?1050:640);
                const QRectF labelRect(labelX,y-28,value.isEmpty()?1100.:650.,56);
                if(focus>.5)glow(labelRect,label,27,(focus-.5)*2); // the focused label carries a soft white glow
                text(labelRect,label,27,QColor(255,255,255,int(255*alpha)));
                if(!value.isEmpty() && panelAmount<.5)text({1300+offset,y-25,440,50},value,21,QColor(255,255,255,int(230*alpha*(1.-2*panelAmount))));
                const auto subtitle=shell_.subtitleText(entry);
                if(!subtitle.isEmpty())text({labelX,y+20,1100,30},subtitle,17,QColor(230,230,230,int(235*alpha)),Qt::AlignLeft|Qt::AlignTop);
                else if(focus>.95 && !entry.detail.isEmpty())text({labelX,y+24,1100,70},entry.detail,18,QColor(225,225,225),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
                p.restore();
            }
            if(level>0 && columnAlpha>0 && panelAmount<.999) {
                p.save();p.setOpacity(columnAlpha*(1.-behind));text({420+ahead*250,480,50,50},QString(QChar(0x2039)),42,Qt::white,Qt::AlignCenter);p.restore();
            }
        }
        };
        if(shell_.message().isEmpty()) { // behind a message dialog the item columns give way (reference 14.4 s)
            if(!outgoing_.isEmpty())drawColumns(outgoing_,categoryFade_,(outgoingCategory_-categoryPosition_)*202,false);
            drawColumns(columns_,1.-categoryFade_,(shell_.category()-categoryPosition_)*202,true);
        }
        if(!shell_.status().isEmpty())text({680,850,1100,50},shell_.status(),26,QColor(230,230,230));
        // Info bar (system_plugin tex_infobar: a translucent white band with a bright bottom edge) framing the clock in the
        // top-right corner; the clock's centre sits at y~104 as in the reference video.
        p.drawImage(QRectF(1490,60,430,88),assets_.image("system_plugin","tex_infobar")); // stretched, as the 1x125 strip is meant
        text({1240,82,550,45},shell_.clockText(),27,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
    }
    if(!shell_.message().isEmpty()) {
        // PS3 dialog (reference frames 14.4 s / 20.6 s): the XMB dims, two thin full-width rules at y~200 and y~880
        // frame the dialog, the text is centred in the upper part, and the button hint sits centred under the lower rule.
        p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,135));
        // Rules: system_plugin tex_msgdialog_bar, brightest at the centre and fading to both ends.
        for(const double y:{199.,879.})p.drawImage(QRectF(0,y,1920,2),assets_.image("system_plugin","tex_msgdialog_bar"));
        auto lines=shell_.message().split('\n');
        const QString heading=lines.size()>2 && lines[1].isEmpty()?lines.takeFirst():QString();
        if(!heading.isEmpty())lines.removeFirst();
        if(!heading.isEmpty())text({260,250,1400,50},heading,25,Qt::white,Qt::AlignHCenter|Qt::AlignVCenter);
        text({260,heading.isEmpty()?260.:320.,1400,540},lines.join('\n'),22,QColor(240,240,240),Qt::AlignHCenter|Qt::AlignTop|Qt::TextWordWrap);
        drawImage(p,"system_plugin","tex_cross",{902,895,24,24});text({936,887,160,40},"OK",21,QColor(235,235,235));
    }
    if(osk_.open)drawOsk(p);
}
void View::keyPressEvent(QKeyEvent* e) {
    if(osk_.open) { // the keyboard owns input: arrows move, Return presses the key (Ctrl+Return accepts), Esc cancels
        switch(e->key()) {
        case Qt::Key_Left:oskMove(-1,0);break;case Qt::Key_Right:oskMove(1,0);break;
        case Qt::Key_Up:oskMove(0,-1);break;case Qt::Key_Down:oskMove(0,1);break;
        case Qt::Key_Return:case Qt::Key_Enter:if(e->modifiers()&Qt::ControlModifier)oskClose(true);else oskPress();break;
        case Qt::Key_Escape:oskClose(false);break;
        case Qt::Key_Backspace:oskBackspace();break;
        default:if(!e->text().isEmpty() && e->text().at(0).isPrint())oskInsert(e->text());break;
        }
        return;
    }
    switch(e->key()) {
    case Qt::Key_Left:shell_.horizontal(-1);break;case Qt::Key_Right:shell_.horizontal(1);break;
    case Qt::Key_Up:shell_.vertical(-1);break;case Qt::Key_Down:shell_.vertical(1);break;
    case Qt::Key_Return:case Qt::Key_Enter:shell_.activate();break;
    case Qt::Key_Escape:case Qt::Key_Backspace:shell_.back();break;
    case Qt::Key_Space:shell_.togglePlayback();break;
    case Qt::Key_PageUp:shell_.vertical(-10);break;case Qt::Key_PageDown:shell_.vertical(10);break;
    case Qt::Key_F5:shell_.refreshDirectory();break;
    case Qt::Key_F11:shell_.setFullscreen(!window()->isFullScreen());break;
    default:QOpenGLWidget::keyPressEvent(e);return;
    }
    e->accept();
}
void View::pollController() {
#ifdef Q_OS_WIN
    if(!isActiveWindow() || QApplication::activeModalWidget()){buttons_=0;return;}
    XINPUT_STATE state{};if(XInputGetState(0,&state)!=ERROR_SUCCESS){buttons_=0;return;}
    const auto buttons=state.Gamepad.wButtons;const auto edge=quint16(buttons&~buttons_);const auto now=clock_.elapsed();
    const bool repeat=now>=repeatAt_;
    const auto movement=quint16(edge|(repeat?buttons:0));
    if(osk_.open) { // ✕ press, ○ cancel, □ backspace, △ space, START accept (the PS3 keyboard's own bindings)
        if(movement&XINPUT_GAMEPAD_DPAD_LEFT)oskMove(-1,0);if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)oskMove(1,0);
        if(movement&XINPUT_GAMEPAD_DPAD_UP)oskMove(0,-1);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)oskMove(0,1);
        if(edge&XINPUT_GAMEPAD_A)oskPress();if(edge&XINPUT_GAMEPAD_B)oskClose(false);
        if(edge&XINPUT_GAMEPAD_X)oskBackspace();if(edge&XINPUT_GAMEPAD_Y)oskInsert(" ");
        if(edge&XINPUT_GAMEPAD_START)oskClose(true);
        if(edge&0x000f)repeatAt_=now+300;else if(movement&0x000f)repeatAt_=now+100;
        buttons_=buttons;return;
    }
    if(movement&XINPUT_GAMEPAD_DPAD_LEFT)shell_.horizontal(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)shell_.horizontal(1);
    if(movement&XINPUT_GAMEPAD_DPAD_UP)shell_.vertical(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_DOWN)shell_.vertical(1);
    if(edge&XINPUT_GAMEPAD_A)shell_.activate();if(edge&XINPUT_GAMEPAD_B)shell_.back();
    if(edge&0x000f)repeatAt_=now+300;else if(movement&0x000f)repeatAt_=now+100;
    buttons_=buttons;
#endif
}
// ---- On-screen keyboard -----------------------------------------------------------------------------------------
// Geometry from the reference (RPCS3 video, 64 s), at 1080p: window x 493-1427, y 290-788; title bar 290-328;
// text field 333-389; a 10x4 character grid from x 500 with a 72 px pitch; two function rows; prediction panel at right.
namespace {
enum class OskAction { None, Symbols, Shift, Backspace, Space, Left, Right, Enter };
struct OskKey { const char* label; int column,span; OskAction action; QColor top,bottom; const char* button; };
const QColor oskYellow1(214,209,150),oskYellow2(176,170,110),oskTeal1(176,206,214),oskTeal2(132,166,176),oskBlue1(140,176,214),oskBlue2(92,128,170);
const OskKey oskFunctionRows[2][8]={
    {{"*%#",0,1,OskAction::Symbols,oskYellow1,oskYellow2,""},{"",1,1,OskAction::None,oskYellow1,oskYellow2,""},
     {"a/à",2,1,OskAction::None,oskTeal1,oskTeal2,"L2"},{"a/A",3,1,OskAction::Shift,oskTeal1,oskTeal2,"R2"},
     {"Backspace",4,2,OskAction::Backspace,oskTeal1,oskTeal2,"□"},{"Space",6,2,OskAction::Space,oskTeal1,oskTeal2,"△"},
     {"",8,2,OskAction::None,oskTeal1,oskTeal2,""},{nullptr,0,0,OskAction::None,{},{},""}},
    {{"▲",0,1,OskAction::None,oskBlue1,oskBlue2,""},{"▼",1,1,OskAction::None,oskBlue1,oskBlue2,""},
     {"◀",2,1,OskAction::Left,oskBlue1,oskBlue2,"L1"},{"▶",3,1,OskAction::Right,oskBlue1,oskBlue2,"R1"},
     {"⊞",4,1,OskAction::None,oskBlue1,oskBlue2,""},{"",5,1,OskAction::None,oskTeal1,oskTeal2,""},
     {"Input Mode",6,2,OskAction::None,oskTeal1,oskTeal2,"SELECT"},{"Enter",8,2,OskAction::Enter,oskTeal1,oskTeal2,"START"}}};
const OskKey* oskFunctionKey(int row,int column) {
    for(const auto& key:oskFunctionRows[row-4])if(key.label && column>=key.column && column<key.column+key.span)return &key;
    return nullptr;
}
constexpr double oskRowY[6]={434,488,542,596,650,704};
}
QString View::oskCharacter(int row,int column) const {
    static const char* lower[4]={"1234567890","qwertyuiop","asdfghjkl'","zxcvbnm,.?"};
    static const char* upper[4]={"1234567890","QWERTYUIOP","ASDFGHJKL\"","ZXCVBNM;:!"};
    static const char* symbols[4]={"!@#$%^&*()","-_=+[]{}\|","/<>`~;:\"'&","@#%*+-=_.,"};
    const char* const* set=osk_.symbols?symbols:osk_.shift?upper:lower;
    return QString(QChar(set[row][column]));
}
void View::openOsk(const QString& title,const QString& initial,std::function<void(QString)> done) {
    osk_=Osk{};osk_.open=true;osk_.title=title;osk_.text=initial;osk_.cursor=int(initial.size());osk_.done=std::move(done);
    update();
}
void View::oskClose(bool accept) {
    auto done=std::move(osk_.done);const auto value=osk_.text.trimmed();osk_=Osk{};
    if(accept && done && !value.isEmpty())done(value);
    update();
}
void View::oskInsert(const QString& value){osk_.text.insert(osk_.cursor,value);osk_.cursor+=int(value.size());if(osk_.text.size()>32){osk_.text.truncate(32);osk_.cursor=std::min(osk_.cursor,32);}update();}
void View::oskBackspace(){if(osk_.cursor>0){osk_.text.remove(osk_.cursor-1,1);--osk_.cursor;}update();}
void View::oskMove(int dx,int dy) {
    osk_.row=(osk_.row+dy+6)%6;
    if(dx!=0) {
        if(osk_.row>=4) { // jump over multi-column keys
            const auto* key=oskFunctionKey(osk_.row,osk_.column);
            osk_.column=dx>0?(key?key->column+key->span:osk_.column+1):(key?key->column-1:osk_.column-1);
        } else osk_.column+=dx;
        osk_.column=(osk_.column+10)%10;
    }
    if(osk_.row>=4)if(const auto* key=oskFunctionKey(osk_.row,osk_.column))osk_.column=key->column;
    update();
}
void View::oskPress() {
    if(osk_.row<4){oskInsert(oskCharacter(osk_.row,osk_.column));if(osk_.shift && !osk_.symbols)osk_.shift=false;return;}
    const auto* key=oskFunctionKey(osk_.row,osk_.column);if(!key)return;
    switch(key->action) {
    case OskAction::Symbols:osk_.symbols=!osk_.symbols;break;
    case OskAction::Shift:osk_.shift=!osk_.shift;break;
    case OskAction::Backspace:oskBackspace();break;
    case OskAction::Space:oskInsert(" ");break;
    case OskAction::Left:osk_.cursor=std::max(0,osk_.cursor-1);break;
    case OskAction::Right:osk_.cursor=std::min(int(osk_.text.size()),osk_.cursor+1);break;
    case OskAction::Enter:oskClose(true);return;
    case OskAction::None:break;
    }
    update();
}
void View::drawOsk(QPainter& p) {
    const auto label=[&](const QRectF& r,const QString& value,int size,QColor colour,int flags=Qt::AlignCenter,bool bold=false) {
        QFont f(shell_.fontFamily());f.setPixelSize(size);f.setBold(bold);f.setHintingPreference(QFont::PreferVerticalHinting);p.setFont(f);p.setPen(colour);p.drawText(r,flags,value);
    };
    const auto key=[&](const QRectF& r,QColor top,QColor bottom,bool focused) {
        QLinearGradient g(r.topLeft(),r.bottomLeft());g.setColorAt(0,focused?QColor(255,255,255):top);g.setColorAt(1,focused?QColor(225,232,236):bottom);
        p.setPen(QPen(QColor(60,60,60,160),1));p.setBrush(g);p.drawRoundedRect(r,5,5);
        if(focused){p.setPen(QPen(QColor(255,255,255,170),3));p.setBrush(Qt::NoBrush);p.drawRoundedRect(r.adjusted(-3,-3,3,3),7,7);}
    };
    p.save();
    p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,120)); // the XMB dims behind the keyboard
    const QRectF window(493,290,934,498);
    p.setPen(QPen(QColor(30,30,30),1.5));p.setBrush(QColor(78,78,80));p.drawRect(window);
    QLinearGradient title(0,290,0,328);title.setColorAt(0,QColor(222,222,222));title.setColorAt(1,QColor(178,178,178));
    p.fillRect(QRectF(494,291,932,37),title);
    label({506,291,40,37},QString(QChar(0x270e)),22,QColor(40,40,40));
    label({540,291,800,37},osk_.title,20,QColor(30,30,30),Qt::AlignLeft|Qt::AlignVCenter);
    p.fillRect(QRectF(494,330,932,60),QColor(52,52,54));
    QFont fieldFont(shell_.fontFamily());fieldFont.setPixelSize(26);p.setFont(fieldFont);
    const double before=QFontMetricsF(fieldFont).horizontalAdvance(osk_.text.left(osk_.cursor));
    label({512,330,900,60},osk_.text,26,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
    if((clock_.elapsed()/500)%2==0)p.fillRect(QRectF(512+before,342,2,36),QColor(140,220,120)); // blinking cursor
    for(int row=0;row<4;++row)for(int column=0;column<10;++column) {
        const QRectF r(500+column*72,oskRowY[row]-23,66,46);
        key(r,QColor(236,236,236),QColor(190,190,190),osk_.row==row && osk_.column==column);
        label(r,oskCharacter(row,column),22,QColor(40,40,40));
    }
    for(int row=4;row<6;++row)for(const auto& k:oskFunctionRows[row-4]) {
        if(!k.label)continue;
        const QRectF r(500+k.column*72,oskRowY[row]-23,k.span*72-6,46);
        key(r,k.top,k.bottom,osk_.row==row && osk_.column==k.column);
        const bool highlighted=(k.action==OskAction::Shift && osk_.shift) || (k.action==OskAction::Symbols && osk_.symbols);
        if(*k.button){label({r.left()+4,r.top()+1,r.width()-8,14},k.button,11,QColor(30,30,30,200),Qt::AlignLeft|Qt::AlignTop);}
        label(r.adjusted(0,*k.button?8:0,0,0),k.label,k.span>1?19:20,highlighted?QColor(10,90,160):QColor(30,40,50),Qt::AlignCenter,highlighted);
    }
    p.fillRect(QRectF(1218,404,197,230),QColor(58,58,60)); // prediction candidates
    p.fillRect(QRectF(1218,637,197,33),QColor(96,104,112));
    label({1226,637,120,33},"Prediction",15,QColor(230,230,230),Qt::AlignLeft|Qt::AlignVCenter);
    p.fillRect(QRectF(494,730,932,57),QColor(64,64,66));
    p.restore();
}
}
