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
#include <cmath>
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
    connect(&shell_,&Shell::requestSystemName,this,[this](const QString& value){
        bool ok=false;const auto name=QInputDialog::getText(this,"System Name","Name",QLineEdit::Normal,value,&ok);
        if(ok)shell_.setSystemName(name);
    });
    connect(&shell_,&Shell::requestNewUser,this,[this]{bool ok=false;const auto name=QInputDialog::getText(this,"Create New User","Name",QLineEdit::Normal,{},&ok);if(ok)shell_.createUser(name);});
    loadWallpaper();
    clock_.start();timer_.setTimerType(Qt::PreciseTimer);timer_.setInterval(16);
    connect(&timer_,&QTimer::timeout,this,[this]{pollController();update();});timer_.start();
}
void View::syncColumns() {
    const int depth=shell_.depth();
    if(previousCategory_!=shell_.category()) {
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
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float wave(float x,float t){return .46+.055*sin(x*4.0-t)+.018*sin(x*7.0+t*.6);}
void main(){
    vec2 p=uv;float t=seconds*.12;
    vec3 c=mix(tint*.16,tint*.58,1.0-smoothstep(0.0,1.0,p.y));
    if(useMonthPalette)c=texture(monthPalette,p).rgb;
    c+=tint*.045*exp(-length((p-vec2(.42,.28))*vec2(.7,1.6))*2.0);
    for(int i=0;i<7;++i){
        float k=float(i);float line=wave(p.x,t)+.019*sin(p.x*4.8+t+k*.50)+k*.0035;
        float d=p.y-line;float w=.010+k*.0016;
        float ribbon=exp(-d*d/(w*w))*.075+exp(-d*d/.008)*.009;
        c+=mix(tint,vec3(.92),.82)*ribbon;
    }
    vec2 grid=vec2(42.0,24.0);vec2 cell=floor(p*grid);
    if(!classic)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 id=cell+vec2(x,y);float seed=hash(id);vec2 centre=(id+vec2(seed,hash(id+51.7)))/grid;
        centre.y+=.004*sin(t*1.8+seed*40.0);
        float weight=exp(-pow((centre.y-wave(centre.x,t))/.16,2.0))*(1.0-.7*centre.x);
        vec2 delta=(p-centre)*resolution;float radius=seed>.98?3.3:.50+seed*.65;
        c+=vec3(.90,.84,.66)*exp(-dot(delta,delta)/(radius*radius))*weight*(.20+.60*seed);
    }
    float grain=fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5;
    c+=grain/650.0;colour=vec4(c,1.0);
})";
    glReady_=background_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)&&background_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)&&background_.link();
    if(!glReady_)qCritical().noquote()<<background_.log();
    glGenVertexArrays(1,&vao_);
    if(!monthImage_.isNull()) {
        monthTexture_=std::make_unique<QOpenGLTexture>(monthImage_.mirrored(false,true));
        monthTexture_->setMinificationFilter(QOpenGLTexture::Linear);monthTexture_->setMagnificationFilter(QOpenGLTexture::Linear);
        monthTexture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    qInfo()<<"native renderer:"<<reinterpret_cast<const char*>(glGetString(GL_RENDERER));
}
void View::paintGL() {
    const qint64 now=clock_.elapsed();
    const double dt=std::min(.1,double(now-previous_)/1000.);previous_=now;
    const double blend=1.-std::exp(-dt*18.);
    categoryPosition_+=(shell_.category()-categoryPosition_)*blend;
    categoryFade_*=std::exp(-dt*18.);if(categoryFade_<.001){categoryFade_=0;outgoing_.clear();}
    depthPosition_+=(shell_.depth()-depthPosition_)*blend;
    for(auto& column:columns_)column.position+=(column.selection-column.position)*blend;
    if(std::abs(depthPosition_-shell_.depth())<.001) {
        depthPosition_=shell_.depth();
        if(columns_.size()>shell_.depth()+1)columns_.resize(shell_.depth()+1);
    }
    glClearColor(.04f,.04f,.05f,1.f);glClear(GL_COLOR_BUFFER_BIT);
    if(glReady_) {
        static const QVector3D colours[]={{.55f,.39f,.13f},{.12f,.36f,.68f},{.40f,.20f,.60f},{.62f,.17f,.18f},{.16f,.43f,.32f},{.48f,.50f,.54f}};
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
        glViewport(0,0,int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));
        background_.bind();background_.setUniformValue("seconds",float(now)/1000.f);
        background_.setUniformValue("tint",colours[std::clamp(shell_.theme(),0,5)]);
        background_.setUniformValue("resolution",QVector2D(float(width()*devicePixelRatioF()),float(height()*devicePixelRatioF())));
        background_.setUniformValue("monthPalette",0);background_.setUniformValue("useMonthPalette",shell_.theme()==0 && bool(monthTexture_));
        background_.setUniformValue("classic",shell_.backgroundMode()==1);
        if(monthTexture_)monthTexture_->bind(0);
        glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);background_.release();
        if(monthTexture_)monthTexture_->release(0);
    }
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double scale=std::min(width()/1920.,height()/1080.);
    painter.translate((width()-1920*scale)/2,(height()-1080*scale)/2);painter.scale(scale,scale);drawScene(painter);
    painter.end();
    ++frames_;if(now-reportAt_>=5000){qInfo()<<"native frames:"<<frames_*1000./double(now-reportAt_)<<"fps; decoded images"<<assets_.decodedImages()<<"missing images"<<assets_.failedImages();frames_=0;reportAt_=now;}
}
void View::drawImage(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal opacity) {
    if(container=="qgl/icontex") {
        p.save();p.setOpacity(opacity);drawCategory(p,Category{texture,{},container,texture,{}},rect);p.restore();return;
    }
    const auto& image=assets_.image(container,texture);
    if(image.isNull())return;
    const QSizeF size=QSizeF(image.size()).scaled(rect.size(),Qt::KeepAspectRatio);
    p.save();p.setOpacity(opacity);p.drawImage(QRectF(rect.center()-QPointF(size.width()/2,size.height()/2),size),image);p.restore();
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
        static const QColor tints[]={QColor(192,179,112),QColor(180,211,240),QColor(211,186,229),QColor(230,184,175),QColor(178,218,189),QColor(214,215,218)};
        const QColor tint=tints[theme];
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
        QFont f(shell_.fontFamily());f.setPixelSize(size);p.setFont(f);p.setPen(QColor(0,0,0,100));p.drawText(r.translated(2,2),flags,value);p.setPen(colour);p.drawText(r,flags,value);
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
            const double focus=std::max(0.,1.-std::abs(distance));const double size=72+focus*40;
            p.save();p.setOpacity(i==shell_.category()?1.:1.-childAmount);
            drawCategory(p,cat,{x-size/2,290-size/2,size,size});
            if(i==shell_.category()){p.setOpacity(1.-childAmount);text({x-140,338,280,45},cat.title,24,Qt::white,Qt::AlignCenter);}
            p.restore();
        }
        const auto drawColumns=[&](const QVector<Column>& columns,double opacity,double offset,bool active) {
        for(int level=0;level<columns.size();++level) {
            const auto& column=columns[level];
            const double behind=std::clamp(depthPosition_-level,0.,1.),ahead=std::clamp(level-depthPosition_,0.,1.);
            if(behind>.999 && level<shell_.depth()-1)continue;
            const double columnAlpha=(1.-ahead)*opacity;
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
                const double size=level==0?52+focus*108:22+focus*20;
                const bool tile=entry.texture.contains("folder") || entry.texture.contains("default") || entry.texture.contains("album");
                const double iconWidth=tile?155+focus*165:size,iconHeight=tile?90+focus*90:size;
                const double iconAlpha=columnAlpha*(1.-behind*.78*(1.-focus));
                drawImage(p,entry.container,entry.texture,{iconX-iconWidth/2,y-iconHeight/2,iconWidth,iconHeight},iconAlpha);
                p.save();p.setOpacity(columnAlpha*(1.-behind));
                const double labelX=(level==0?(tile?756:680):620)+ahead*250+offset;
                const double alpha=.36+.64*focus;
                const auto value=active?shell_.valueText(entry):QString{};
                QFont labelFont(shell_.fontFamily());labelFont.setPixelSize(int(28+focus*8));
                const auto label=QFontMetrics(labelFont).elidedText(entry.title,Qt::ElideRight,value.isEmpty()?1050:640);
                text({labelX,y-28,value.isEmpty()?1100.:650.,56},label,int(28+focus*8),QColor(255,255,255,int(255*alpha)));
                if(!value.isEmpty())text({1300+offset,y-25,440,50},value,24,QColor(255,255,255,int(240*alpha)));
                if(focus>.95 && !entry.detail.isEmpty())text({labelX,y+23,1100,82},entry.detail,21,QColor(220,220,220),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
                p.restore();
            }
            if(level>0 && columnAlpha>0) {
                p.save();p.setOpacity(columnAlpha*(1.-behind));text({420+ahead*250,480,50,50},QString(QChar(0x2039)),42,Qt::white,Qt::AlignCenter);p.restore();
            }
        }
        };
        if(!outgoing_.isEmpty())drawColumns(outgoing_,categoryFade_,(outgoingCategory_-categoryPosition_)*202,false);
        drawColumns(columns_,1.-categoryFade_,(shell_.category()-categoryPosition_)*202,true);
        if(!shell_.status().isEmpty())text({680,850,1100,50},shell_.status(),26,QColor(230,230,230));
        text({1240,103,550,45},shell_.clockText(),27,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
        drawImage(p,"system_plugin","tex_circle",{1520,1010,26,26});text({1558,1004,210,40},"Back",23,QColor(225,225,225));
        drawImage(p,"system_plugin","tex_cross",{1310,1010,26,26});text({1348,1004,160,40},"Enter",23,QColor(225,225,225));
    }
    if(!shell_.message().isEmpty()) {
        p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,150));
        QLinearGradient gradient(0,370,0,760);gradient.setColorAt(0,QColor(65,65,67,245));gradient.setColorAt(1,QColor(23,23,25,245));
        p.fillRect(QRectF(0,365,1920,400),gradient);p.setPen(QColor(255,255,255,80));p.drawLine(0,365,1920,365);p.drawLine(0,765,1920,765);
        text({340,390,1240,320},shell_.message(),28,Qt::white,Qt::AlignLeft|Qt::AlignVCenter|Qt::TextWordWrap);
    }
}
void View::keyPressEvent(QKeyEvent* e) {
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
    if(movement&XINPUT_GAMEPAD_DPAD_LEFT)shell_.horizontal(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)shell_.horizontal(1);
    if(movement&XINPUT_GAMEPAD_DPAD_UP)shell_.vertical(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_DOWN)shell_.vertical(1);
    if(edge&XINPUT_GAMEPAD_A)shell_.activate();if(edge&XINPUT_GAMEPAD_B)shell_.back();
    if(edge&0x000f)repeatAt_=now+300;else if(movement&0x000f)repeatAt_=now+100;
    buttons_=buttons;
#endif
}
}
