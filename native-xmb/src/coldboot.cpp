#include "coldboot.h"
#include "design_space.h"
#include "view.h"
#include <QAudioDecoder>
#include <QAudioSink>
#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QMediaDevices>
#include <QFontMetricsF>
#include <QOpenGLPaintDevice>
#include <QTimer>
#include <QPainter>
#include <QTextLayout>
#include <QtEndian>
#include <QUrl>
#include <QtMath>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>

namespace xmb {
namespace {
quint32 be32(const QByteArray& b,qint64 o) { return o+4<=b.size()?qFromBigEndian<quint32>(b.constData()+o):0xffffffffu; }
quint16 be16(const QByteArray& b,qint64 o) { return o+2<=b.size()?qFromBigEndian<quint16>(b.constData()+o):0; }
float bef(const QByteArray& b,qint64 o) { const quint32 v=be32(b,o);float f;std::memcpy(&f,&v,4);return f; }
float half(quint16 h) {
    const int e=(h>>10)&31,m=h&1023;const float s=h>>15?-1.f:1.f;
    return e?s*std::ldexp(1.f+m/1024.f,e-15):s*std::ldexp(m/1024.f,-14);
}
// Cubic-Bezier time curve through (0,0), (x1,y1), (x2,y2), (1,1): y at x = u.
double bezier(const QVector4D& c,double u) {
    const auto f=[](double a,double b,double s){return 3*a*s*(1-s)*(1-s)+3*b*s*s*(1-s)+s*s*s;};
    double lo=0,hi=1,s=u;
    for(int i=0;i<40;++i){s=(lo+hi)/2;(f(c.x(),c.z(),s)<u?lo:hi)=s;}
    return f(c.y(),c.w(),s);
}
}

Coldboot::Coldboot()=default;
Coldboot::~Coldboot()=default;

QVector4D Coldboot::Track::at(double t) const {
    if(duration<=0 || t>=start+duration)return t>=start?to:from;
    if(t<=start)return from;
    const double u=(t-start)/duration;
    return from+(to-from)*float(curved?bezier(curve,u):u);
}

// RAF ("_RAF", u32 BE size, zlib) inflates to "RAFO": u32 version and six (offset, size) sections: 0 the element
// tree, 1 object names (u32 element offset + NUL string, packed), 2 keys (NUL strings), 4 floats, 5 file data.
// An element is name key, attribute count, parent, previous, next, first child, last child (u32, -1 = none), then
// 16-byte attributes: key, type (1 int, 2 float, 3 key string, 5 float vector [index, count], 6 data [offset, size],
// 7 object id, 8 reference, both offsets into the name section), a, b. work/coldboot-raf-dump.ts prints the tree.
bool Coldboot::parseRaf(const QByteArray& file,QOpenGLFunctions_3_3_Core& gl) {
    if(!file.startsWith("_RAF"))return false;
    const QByteArray raf=qUncompress(file.mid(4)); // qUncompress takes the same u32 BE size + zlib stream
    if(!raf.startsWith("RAFO"))return false;
    quint32 off[6];
    for(int i=0;i<6;++i)off[i]=be32(raf,8+i*8);
    const auto cstr=[&](qint64 o){return QString::fromLatin1(raf.constData()+o);};
    const auto key=[&](quint32 k){return cstr(off[2]+k);};
    const auto nameElement=[&](quint32 k){return be32(raf,off[1]+k);};
    struct Attr { quint32 type=0,a=0,b=0; };
    struct Element { QString tag; QHash<QString,Attr> attrs; quint32 parent=~0u; };
    QHash<quint32,Element> elements;
    const std::function<void(quint32)> walk=[&](quint32 e) {
        for(;e!=0xffffffffu && off[0]+e+28<=quint32(raf.size());e=be32(raf,off[0]+e+16)) {
            const quint32 o=off[0]+e;Element element{key(be32(raf,o)),{},be32(raf,o+8)};
            for(quint32 i=0,n=be32(raf,o+4);i<n;++i){const quint32 p=o+28+i*16;element.attrs.insert(key(be32(raf,p)),{be32(raf,p+4),be32(raf,p+8),be32(raf,p+12)});}
            elements.insert(e,element);
            walk(be32(raf,o+20));
        }
    };
    walk(0);
    const auto vec=[&](const Element& el,const char* name){
        QVector4D v;const Attr a=el.attrs.value(name);
        if(a.type==5)for(quint32 i=0;i<a.b && i<4;++i)v[int(i)]=bef(raf,off[4]+(a.a+i)*4);
        else if(a.type==2){float f;std::memcpy(&f,&a.a,4);v=QVector4D(f,0,0,0);}
        return v;
    };
    const auto refKey=[&](const Element& el,const char* name){return be32(raf,off[1]+el.attrs.value(name).a);};
    const auto ref=[&](const Element& el,const char* name){return elements.value(refKey(el,name));};
    const auto child=[&](quint32 parent,const char* tag){for(auto it=elements.cbegin();it!=elements.cend();++it)if(it->parent==parent && it->tag==tag)return it.value();return Element{};};
    const auto data=[&](const Element& fileElement){const Attr a=fileElement.attrs.value("src");return raf.mid(off[5]+a.a,a.b);};
    // GTF: header, then per texture id, offset, size and CellGcmTexture (format, mipmap, dimension, cubemap, remap,
    // width, height, depth, location, pitch). The logo textures are B8 linear (0xa1), each mip level at the same pitch.
    const auto texture=[&](const QByteArray& gtf)->GLuint {
        if(gtf.size()<0x30 || be32(gtf,8)<1)return 0;
        const quint32 dataOffset=be32(gtf,0x10);const quint8 format=quint8(gtf.at(0x18)),mips=quint8(gtf.at(0x19));
        const int width=be16(gtf,0x20),height=be16(gtf,0x22);const quint32 pitch=be32(gtf,0x28);
        if(format!=0xa1 || !width || !height || pitch<quint32(width)){qWarning()<<"coldboot: unexpected GTF format"<<Qt::hex<<format;return 0;}
        GLuint id=0;gl.glGenTextures(1,&id);gl.glBindTexture(GL_TEXTURE_2D,id);
        gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,GLint(pitch));
        quint32 at=dataOffset;int levels=0;
        for(int level=0;level<std::max<int>(1,mips);++level) {
            const int w=std::max(1,width>>level),h=std::max(1,height>>level);
            if(at+pitch*quint32(h)>quint32(gtf.size()))break;
            gl.glTexImage2D(GL_TEXTURE_2D,level,GL_R8,w,h,0,GL_RED,GL_UNSIGNED_BYTE,gtf.constData()+at);
            at+=pitch*quint32(h);++levels;
        }
        gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);gl.glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        const GLint swizzle[]={GL_RED,GL_RED,GL_RED,GL_RED}; // B8 reads as one channel in every component
        gl.glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,swizzle);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,std::max(0,levels-1));
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,levels>1?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT); // wrap 1
        gl.glBindTexture(GL_TEXTURE_2D,0);
        return id;
    };
    QByteArray script;
    const char* names[]={"logo","footer","blur"};
    for(auto it=elements.cbegin();it!=elements.cend();++it) {
        const Element& el=it.value();
        const QString id=cstr(off[1]+el.attrs.value("id").a+4);
        if(el.tag=="actor") {
            for(size_t i=0;i<actors_.size();++i)if(id==names[i]) {
                Actor& a=actors_[i];a.name=id;
                a.position=vec(el,"position").toVector3D();a.scale=vec(el,"scale").toVector3D();a.rotation=vec(el,"rotation").toVector3D();
                const QVector4D uvo=vec(el,"uv_offset"),uvs=vec(el,"uv_scale");a.uvOffset={uvo.x(),uvo.y()};a.uvScale={uvs.x(),uvs.y()};
                // material -> its _texture child -> texture -> file
                a.texture=texture(data(ref(ref(child(refKey(el,"material"),"_texture"),"texref"),"fileref")));
                if(i==0 && !parsePlane(data(ref(child(refKey(el,"model"),"geometry"),"fileref"))))return false;
            }
        } else if(el.tag=="camera") {
            eye_=vec(el,"position").toVector3D();target_=eye_+vec(el,"direction").toVector3D();up_=vec(el,"up").toVector3D();
            yfov_=vec(el,"yfov").x();znear_=vec(el,"znear").x();zfar_=vec(el,"zfar").x();
        } else if(el.tag=="light" && id=="pointlight") {
            lightAttenuation_=vec(el,"attenuation").toVector3D();
        } else if(el.tag=="script") {
            script=data(ref(el,"fileref"));
        }
    }
    for(const auto& a:actors_)if(!a.texture){qWarning()<<"coldboot: RAF actor"<<a.name<<"has no texture";return false;}
    return readScript(script);
}

// plane.edge, an Edge geometry file with one segment (offset at +4): vertex and index counts at segment +8/+10,
// float3 BE positions at the offset in segment +0x18, normals packed 11/11/10 at +0x1c, half2 BE uvs at +0x2c. The
// index stream is Edge-compressed and not decoded: the 16 vertices form a regular 4x4 grid (uv = ((x+1)/2, (1-z)/2),
// work/coldboot-edge-plane.ts), triangulated here by their uv.
bool Coldboot::parsePlane(const QByteArray& edge) {
    const quint32 seg=be32(edge,4);const int count=be16(edge,seg+8);
    const quint32 pos=be32(edge,seg+0x18),normal=be32(edge,seg+0x1c),uv=be32(edge,seg+0x2c);
    if(count!=16 || uv+count*4>quint32(edge.size())){qWarning()<<"coldboot: unexpected plane.edge layout";return false;}
    const auto snorm=[](quint32 v,int bits){const int s=int(v<<(32-bits))>>(32-bits);return float(s)/float((1<<(bits-1))-1);};
    int grid[4][4];for(auto& row:grid)for(int& i:row)i=-1;
    planeVertices_.clear();
    for(int i=0;i<count;++i) {
        const quint32 n=be32(edge,normal+i*4);
        const float u=half(be16(edge,uv+i*4)),v=half(be16(edge,uv+i*4+2));
        planeVertices_.insert(planeVertices_.end(),{bef(edge,pos+i*12),bef(edge,pos+i*12+4),bef(edge,pos+i*12+8),
            snorm(n&0x7ff,11),snorm((n>>11)&0x7ff,11),snorm(n>>22,10),u,v});
        const int gu=int(std::lround(u*3)),gv=int(std::lround(v*3));
        if(gu<0 || gu>3 || gv<0 || gv>3 || grid[gv][gu]>=0){qWarning()<<"coldboot: plane.edge is not a 4x4 grid";return false;}
        grid[gv][gu]=i;
    }
    planeIndices_.clear();
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)
        planeIndices_.insert(planeIndices_.end(),{GLushort(grid[r][c]),GLushort(grid[r][c+1]),GLushort(grid[r+1][c]),GLushort(grid[r+1][c]),GLushort(grid[r][c+1]),GLushort(grid[r+1][c+1])});
    return true;
}

// coldboot.jsx (VSMX: little-endian header with code at +8/+0xc and 8-bit names at +0x28/+0x2c; 8-byte
// instructions, opcode = id & 0xff). Its timing constants are assignments "name = <float>" (VARIABLE 0x2e, CONST_FLOAT
// 0x27, optional NEGATE 0x08, ASSIGN 0x01); the rest of the script (work/coldboot-vsmx-decompile.ts) is the timeline
// in state().
bool Coldboot::readScript(const QByteArray& vsmx) {
    if(!vsmx.startsWith("VSMX"))return false;
    const auto le=[&](qint64 o){return o+4<=vsmx.size()?qFromLittleEndian<quint32>(vsmx.constData()+o):0u;};
    const QList<QByteArray> names=vsmx.mid(le(0x28),le(0x2c)).split('\0');
    const quint32 code=le(0x8),count=le(0xc)/8;
    QHash<QByteArray,double> values;
    for(quint32 i=0;i+2<count;++i) {
        const auto op=[&](quint32 k){return le(code+k*8)&0xff;};const auto val=[&](quint32 k){return le(code+k*8+4);};
        if(op(i)!=0x2e || op(i+1)!=0x27 || val(i)>=quint32(names.size()))continue;
        const bool negate=op(i+2)==0x08;
        if(op(i+2+(negate?1:0))!=0x01)continue;
        const quint32 bits=val(i+1);float f;std::memcpy(&f,&bits,4);
        values.insert(names[int(val(i))],negate?-f:f);
    }
    for(const char* name:{"first_wait","appear_duration","appear_wait","blur_duration","blur_wait","whole_duration","whole_wait","fade_duration"})
        if(!values.contains(name)){qWarning()<<"coldboot: coldboot.jsx has no"<<name;return false;}
    firstWait_=values["first_wait"];appearDuration_=values["appear_duration"];appearWait_=values["appear_wait"];
    blurDuration_=values["blur_duration"];blurWait_=values["blur_wait"];wholeDuration_=values["whole_duration"];
    wholeWait_=values["whole_wait"];fadeDuration_=values["fade_duration"];
    appearStart_=firstWait_;blurStart_=appearStart_+appearDuration_+appearWait_;
    wholeStart_=blurStart_+blurDuration_+blurWait_;fadeStart_=wholeStart_+wholeDuration_+wholeWait_;
    scriptEnd_=fadeStart_+fadeDuration_;
    return true;
}

// coldboot_stereo.ac3 (custom_render_plugin picks _stereo when the audio output is 2-channel, else _multi), decoded
// up front so that it starts with the animation.
void Coldboot::decodeSound(const QString& path) {
    decoder_=std::make_unique<QAudioDecoder>();
    decoder_->setSource(QUrl::fromLocalFile(path));
    QObject::connect(decoder_.get(),&QAudioDecoder::bufferReady,decoder_.get(),[this]{
        while(decoder_->bufferAvailable()) {
            const QAudioBuffer buffer=decoder_->read();
            if(!buffer.isValid())break;
            pcmFormat_=buffer.format();pcm_.append(buffer.constData<char>(),buffer.byteCount());
        }
    });
    QObject::connect(decoder_.get(),&QAudioDecoder::finished,decoder_.get(),[this]{soundReady_=true;});
    QObject::connect(decoder_.get(),qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),decoder_.get(),[this](QAudioDecoder::Error){
        qWarning().noquote()<<"coldboot: sound:"<<decoder_->errorString();soundReady_=true;pcm_.clear();
    });
    decoder_->start();
}

bool Coldboot::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    QFile file(QDir(assets.root()).filePath("vsh/resource/coldboot.raf"));
    if(!file.open(QIODevice::ReadOnly)){qWarning()<<"coldboot: coldboot.raf unavailable";return false;}
    QString error;
    try {
        vertexCg_=CgProgram::load(assets.qglFile("raf","lib/raf/vert0.vpo"),&error);
        if(vertexCg_.valid())fragmentCg_=CgProgram::load(assets.qglFile("raf","lib/raf/sce01.fpo"),&error);
    } catch(const std::exception& e) { error=e.what(); }
    if(!vertexCg_.valid() || !fragmentCg_.valid()){qWarning().noquote()<<"coldboot: raf.qrc vert0/sce01 unavailable:"<<error;return false;}
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexCg_.glsl) || !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentCg_.glsl) || !program_.link()) {
        qWarning().noquote()<<"coldboot: link failed:"<<program_.log();return false;
    }
    if(!parseRaf(file.readAll(),gl)){qWarning()<<"coldboot: coldboot.raf could not be read";return false;}
    // Plane: in_0 position, in_2 normal, in_8 uv; colour (in_3) and tangent (in_14) are constant attributes.
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);gl.glGenBuffers(1,&ibo_);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(planeVertices_.size()*sizeof(float)),planeVertices_.data(),GL_STATIC_DRAW);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_);
    gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(planeIndices_.size()*sizeof(GLushort)),planeIndices_.data(),GL_STATIC_DRAW);
    indexCount_=GLsizei(planeIndices_.size());
    const GLsizei stride=8*sizeof(float);
    gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,nullptr);
    gl.glEnableVertexAttribArray(2);gl.glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(3*sizeof(float)));
    gl.glEnableVertexAttribArray(8);gl.glVertexAttribPointer(8,2,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(6*sizeof(float)));
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);
    initLighting(gl);
    decodeSound(QDir(assets.root()).filePath("vsh/resource/coldboot_stereo.ac3"));
    if(noticeEnabled_) {
        // The local firmware's captures (RPCS3 and the October recompiled-vsh run, docs/screenshots/03) show the
        // SCEE string, so this dev_flash's region is an SCEE one; the generic string is the fallback.
        noticeText_=assets.text("sysconf_plugin","msg_health_warning_start_scee");
        if(noticeText_.isEmpty()){noticeText_=assets.text("sysconf_plugin","msg_health_warning_start");noticeLock_=1.0;noticeHold_=3.0;}
        noticeText_.replace("\\n","\n");
        noticeFamily_=assets.fontFamily();
        if(noticeText_.isEmpty())qWarning()<<"coldboot: sysconf_plugin has no msg_health_warning_start string; no notice";
    }
    noticeCloseAt_=showGui+noticeHold_;
    ready_=true;
    qInfo().noquote()<<QString("coldboot: coldboot.raf scene and coldboot.jsx timeline (appear %1 s, blur %2 s, whole %3 s, fade %4-%5 s)")
        .arg(appearStart_).arg(blurStart_).arg(wholeStart_).arg(fadeStart_).arg(scriptEnd_);
    return true;
}

void Coldboot::start() {
    if(!ready_ || started_)return;
    started_=true;clock_.start();
    if(pcm_.isEmpty()){qWarning()<<"coldboot: no sound (coldboot_stereo.ac3 not decoded)";return;}
    pcmBuffer_=std::make_unique<QBuffer>(&pcm_);pcmBuffer_->open(QIODevice::ReadOnly);
    sink_=std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(),pcmFormat_);
    sink_->start(pcmBuffer_.get());
    qInfo().noquote()<<QString("coldboot: playing coldboot_stereo.ac3, %1 Hz, %2 channels, %3 s").arg(pcmFormat_.sampleRate()).arg(pcmFormat_.channelCount())
        .arg(double(pcm_.size())/std::max(1,pcmFormat_.bytesForFrames(1))/std::max(1,pcmFormat_.sampleRate()),0,'f',2);
}

void Coldboot::skip() {
    if(GameBoot::active()){GameBoot::skip();return;}
    const double t=seconds();
    if(noticeShown() && t>=showGui && t<1e8) {
        // 0x5ab6c / 0x5abcc: ignored while the lock flag is set, else cancel the auto-close timer and hide (200 ms).
        const bool locked=t<showGui+noticeLock_;
        if(!locked && t<noticeCloseAt_)noticeCloseAt_=t;
        qInfo().noquote()<<QString("coldboot: notice press at %1 s (%2)").arg(t,0,'f',3).arg(locked?"input lock, ignored":"closing");
        return;
    }
    skipped_=true;
    if(sink_)sink_->stop();
}

bool Coldboot::holdsGui() const {
    if(GameBoot::active())return true;
    if(!ready_)return false;
    const double t=seconds();
    return t<showGui || (noticeShown() && t<noticeCloseAt_+noticeFade);
}

// Shown at once (FUN_0005bdf8 shows the widget with 0x14b518 = 0), held until noticeCloseAt_, then hidden over
// 200 ms. The hide's curve is paf's (paf_73CF3BB9) and not traced: linear here (INFERRED).
double Coldboot::noticeAlpha(double t) const {
    if(!noticeShown() || t<showGui || t>1e8)return 0;
    if(t<noticeCloseAt_)return 1;
    return std::max(0.,1-(t-noticeCloseAt_)/noticeFade);
}

double Coldboot::seconds() const {
    if(skipped_ || !ready_)return 1e9;
    return started_?clock_.nsecsElapsed()/1e9:0;
}

// raf.sprx's sce01 inputs that the RAF does not carry, as an RPCS3 capture of the cold boot shows them (every one of
// the 1902 draws): _DiffuseWarp a 64x16 ramp (texel i = round(255 i / 63)), _SpecularMap 32x32 white.
void Coldboot::initLighting(QOpenGLFunctions_3_3_Core& gl) {
    std::vector<quint8> ramp(64*16*4);
    for(int y=0;y<16;++y)for(int x=0;x<64;++x){quint8* p=&ramp[size_t((y*64+x)*4)];p[0]=p[1]=p[2]=quint8(std::lround(255.*x/63));p[3]=255;}
    const std::vector<quint8> white(32*32*4,255);
    const auto upload=[&](GLuint& id,int w,int h,const quint8* data){
        gl.glGenTextures(1,&id);gl.glBindTexture(GL_TEXTURE_2D,id);
        gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    };
    upload(warp_,64,16,ramp.data());upload(white_,32,32,white.data());
    gl.glBindTexture(GL_TEXTURE_2D,0);
}

// coldboot.jsx, HD branch (System.resolution 720 is the SD output width; br = 1):
//   logo.setColor(alpha 1, appear_start_time)                                    linear from 0
//   at appear_start: light to logo.position + (0, .5, 0) from + (-3, .5, 0), attenuation to (.1, .1, .1), colour to
//                    white, all over appear_duration, linear
//   at blur_start:   blur alpha to 1 over blur_duration, Bezier (.1, 0, .1, 1)
//   at whole_start:  blur alpha to 0, same curve; footer alpha to 1, linear, both over whole_duration
//   at fade_start:   footer and logo alpha to 0, light + (1.5 x 1.5, 1 x 1.5, 0), light colour to black and
//                    attenuation to (0, 1, 4), over fade_duration, Bezier (.8, 0, .8, 1)
// The curve form (CSS-style cubic Bezier through (0,0)-(1,1)) and every start time agree with the RPCS3 capture
// (work/coldboot-series-timing.ts).
void Coldboot::state(double t,std::array<QVector4D,3>& colours,QVector3D& lightPosition,QVector3D& lightColour,QVector3D& attenuation) const {
    struct Step { double start,duration; QVector4D to; QVector4D curve; };
    static const QVector4D linear{-1,0,0,0},appear{.1f,0,.1f,1},fade{.8f,0,.8f,1};
    // A property's value at t after the steps (each one starts from the value its predecessors reached).
    const auto run=[](double at,QVector4D value,std::initializer_list<Step> steps){
        const std::vector<Step> list(steps);
        std::function<QVector4D(double,size_t)> valueAt=[&](double time,size_t count) {
            QVector4D v=value;
            for(size_t i=0;i<count && i<list.size() && time>=list[i].start;++i) {
                const Step& s=list[i];
                v=Track{valueAt(s.start,i),s.to,s.start,s.duration,s.curve,s.curve.x()>=0}.at(time);
            }
            return v;
        };
        return valueAt(at,list.size());
    };
    const QVector3D logo=actors_[0].position;
    const QVector4D lightStart(logo+QVector3D(-3,.5f,0),0),lightEnd(logo+QVector3D(0,.5f,0),0);
    colours[0]=QVector4D(1,1,1,run(t,{},{{0,appearStart_,{0,0,0,1},linear},{fadeStart_,fadeDuration_,{},fade}}).w());
    colours[1]=QVector4D(1,1,1,run(t,{},{{wholeStart_,wholeDuration_,{0,0,0,1},linear},{fadeStart_,fadeDuration_,{},fade}}).w());
    colours[2]=QVector4D(1,1,1,run(t,{},{{blurStart_,blurDuration_,{0,0,0,1},appear},{wholeStart_,wholeDuration_,{},appear}}).w());
    lightPosition=run(t,lightStart,{{appearStart_,appearDuration_,lightEnd,linear},{fadeStart_,fadeDuration_,lightEnd+QVector4D(1.5f*1.5f,1.f*1.5f,0,0),fade}}).toVector3D();
    lightColour=run(t,{},{{appearStart_,appearDuration_,{1,1,1,0},linear},{fadeStart_,fadeDuration_,{},fade}}).toVector3D();
    attenuation=run(t,QVector4D(lightAttenuation_,0),{{appearStart_,appearDuration_,{.1f,.1f,.1f,0},linear},{fadeStart_,fadeDuration_,{0,1,4,0},fade}}).toVector3D();
}

// The RAF scene with vert0.vpo / sce01.fpo, uniforms as raf.sprx sets them (RPCS3 capture): vc[256..259] the
// model-view-projection rows, vc[260..263] the model rows, vc[466] _UVTransform (uv offset, uv scale), vc[467] _Eye;
// _SpotLight[i] = (light position i, attenuation i, -, specular 1), _LightingMisc[i] = (ambient i, -, light colour i, 1),
// _Material[0..2] = (1, 1, 0, .12), (1, .7, 6, 3), (1, 40, 0, 0): diffuse white, kd 1, ks .7, power 40 (constant in the
// capture), _Material[3].x = the actor colour's alpha. The colour attribute is white (the logo renders white).
void Coldboot::render(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight,GLuint targetFbo) {
    GameBoot::render(gl,pixelWidth,pixelHeight,targetFbo);
    if(GameBoot::active())return;
    if(!started_ || pixelWidth<=0 || pixelHeight<=0)return;
    if(const double alpha=noticeAlpha(seconds());alpha>0)renderNotice(gl,pixelWidth,pixelHeight,targetFbo,alpha);
    if(!logoVisible())return;
    std::array<QVector4D,3> colours;QVector3D lightPosition,lightColour,attenuation;
    state(seconds(),colours,lightPosition,lightColour,attenuation);
    QMatrix4x4 projection,view;
    projection.perspective(qRadiansToDegrees(yfov_),1920.f/1080.f,znear_,zfar_);
    view.lookAt(eye_,target_,up_);
    QVector<QVector4D> fc=fragmentCg_.constants;
    const auto set=[&](const char* name,const QVector4D& v){for(int slot:fragmentCg_.parameters.value(name).constantSlots)if(slot<fc.size())fc[slot]=v;};
    for(int i=0;i<3;++i) {
        set(QString("_SpotLight[%1]").arg(i).toLatin1().constData(),{lightPosition[i],attenuation[i],0,1});
        set(QString("_LightingMisc[%1]").arg(i).toLatin1().constData(),{0,0,lightColour[i],1});
    }
    set("_Material[0]",{1,1,0,.12f});set("_Material[1]",{1,.7f,6,3});set("_Material[2]",{1,40,0,0});
    std::vector<QVector4D> vc(468);
    vc[467]=QVector4D(eye_,1);
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);DesignSpace(pixelWidth,pixelHeight).viewport(gl);
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_SCISSOR_TEST);gl.glDisable(GL_CULL_FACE);
    gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    program_.bind();
    program_.setUniformValue("tex0",0);program_.setUniformValue("tex1",1);program_.setUniformValue("tex2",2);
    gl.glActiveTexture(GL_TEXTURE1);gl.glBindTexture(GL_TEXTURE_2D,warp_);
    gl.glActiveTexture(GL_TEXTURE2);gl.glBindTexture(GL_TEXTURE_2D,white_);
    gl.glBindVertexArray(vao_);
    program_.setAttributeValue(3,1.f,1.f,1.f,1.f);program_.setAttributeValue(14,1.f,0.f,0.f,0.f);
    for(size_t i=0;i<actors_.size();++i) { // RAF order: logo, footer, blur
        const Actor& a=actors_[i];
        if(colours[i].w()<=0)continue;
        QMatrix4x4 model;model.translate(a.position);
        model.rotate(qRadiansToDegrees(a.rotation.z()),0,0,1);model.rotate(qRadiansToDegrees(a.rotation.y()),0,1,0);model.rotate(qRadiansToDegrees(a.rotation.x()),1,0,0);
        model.scale(a.scale);
        const QMatrix4x4 mvp=projection*view*model;
        for(int r=0;r<4;++r){vc[size_t(256+r)]=mvp.row(r);vc[size_t(260+r)]=model.row(r);}
        vc[466]=QVector4D(a.uvOffset.x(),a.uvOffset.y(),a.uvScale.x(),a.uvScale.y());
        set("_Material[3]",{colours[i].w(),0,0,0});
        program_.setUniformValueArray("vc",vc.data(),int(vc.size()));program_.setUniformValueArray("fc",fc.constData(),fc.size());
        gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,a.texture);
        gl.glDrawElements(GL_TRIANGLES,indexCount_,GL_UNSIGNED_SHORT,nullptr);
    }
    gl.glBindVertexArray(0);
    for(int unit=2;unit>=0;--unit){gl.glActiveTexture(GLenum(GL_TEXTURE0+unit));gl.glBindTexture(GL_TEXTURE_2D,0);}
    program_.release();
    gl.glDisable(GL_BLEND);
}

// The notice's text, laid out as the firmware frame shows it (docs/screenshots/03-photosensitivity-notice.jpg, a
// 1280x720 frame of the October recompiled-vsh run drawing sysconf_plugin's own widget; measured, in 1920x1080
// units): white Rodin, cap height 18, line pitch 34, ink starting at x 322.5, wrapped inside a box symmetric about
// the centre (322.5 .. 1597.5: line 3 ends at 1590, line 2 would need ~1330 for its next word), the block's cap
// top to last baseline centred on y 540 (first cap top 478.5, last baseline 598.5). sysconf_plugin builds this
// widget in code ("body" / "body_text", FUN_0005bf30), so no RCO object carries the rect. Drawn over the boot
// background with no plate (the frame is black because that run drew no background; INFERRED), no shadow (INFERRED).
void Coldboot::renderNotice(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight,GLuint targetFbo,double alpha) {
    constexpr qreal left=322.5,width=1275,capHeight=18,pitch=34,centreY=540;
    QFont font(noticeFamily_);font.setPixelSize(100);
    const qreal capAt100=QFontMetricsF(font).capHeight();
    font.setPixelSize(std::max(1,int(std::lround(capHeight*100/std::max<qreal>(1,capAt100)))));
    const QFontMetricsF metrics(font);
    // Lines: each paragraph word-wrapped to the box (QTextLayout shapes; the pitch is the measured one).
    std::vector<std::unique_ptr<QTextLayout>> layouts;int lines=0;
    for(const QString& paragraph:noticeText_.split('\n')) {
        auto layout=std::make_unique<QTextLayout>(paragraph,font);
        QTextOption option;option.setWrapMode(QTextOption::WordWrap);layout->setTextOption(option);
        layout->beginLayout();
        for(QTextLine line=layout->createLine();line.isValid();line=layout->createLine()){line.setLineWidth(width);line.setPosition({0,lines*pitch});++lines;}
        layout->endLayout();
        layouts.push_back(std::move(layout));
    }
    if(!lines)return;
    const qreal firstBaseline=centreY-((lines-1)*pitch+capHeight)/2+capHeight;
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,pixelWidth,pixelHeight);
    QOpenGLPaintDevice device(QSize(pixelWidth,pixelHeight));
    QPainter painter(&device);
    painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
    painter.setTransform(DesignSpace(pixelWidth,pixelHeight).transform());
    painter.setOpacity(alpha);painter.setPen(Qt::white);
    // Line k's top is k * pitch in its layout, so its baseline lands on firstBaseline + k * pitch.
    for(const auto& layout:layouts)layout->draw(&painter,{left,firstBaseline-metrics.ascent()});
    painter.end();
}

// ---- Game boot and the Auto-Off guide -------------------------------------------------------------------------
namespace {
struct GameBootState {
    Assets* assets=nullptr;
    bool active=false;
    QElapsedTimer clock;
    quint64 generation=0;
    std::function<void()> launch;
    // Auto-Off guide: the feed (main.cpp, 4 times a second) and the hide ramp's start.
    qint64 idleMs=0;int minutes=0;bool allowed=false;QString family;
    bool guideShown=false;QElapsedTimer guideHidden;
    QString guideKey;QImage guideImage;
    double guideAmount() {
        const bool want=allowed && GameBoot::guideDue(idleMs,minutes);
        if(want){guideShown=true;return 1;}
        if(guideShown){guideShown=false;guideHidden.start();}
        if(!guideHidden.isValid())return 0;
        return std::max(0.,1-double(guideHidden.elapsed())/GameBoot::guideHideMs);
    }
};
GameBootState& gameBoot() { static GameBootState state;return state; }
bool gameBootDisabled() { static const bool off=qEnvironmentVariableIntValue("XMB_NO_GAMEBOOT")!=0;return off; }
// XMB_GAMEBOOT_AT=<seconds> (local verification, the pinned-state gate): the sequence is held at that time of
// anim_gameboot. The clock does not run, the Finished timer is not armed, a press does not skip it and the title is
// never launched, so a frame grabbed during it is the same on every run. -1 when unset.
double gameBootPinned() {
    static const double at=[]{bool ok=false;const double v=qEnvironmentVariable("XMB_GAMEBOOT_AT").toDouble(&ok);return ok && v>=0?v:-1.;}();
    return at;
}
float bits(quint32 v) { return std::bit_cast<float>(v); }
}

// anim_gameboot's Fade and Resize on the ps3logo plane (offsets 2604..3260 of custom_render_plugin.rco): at 0 the plane
// is at alpha 0, scale 0.93835 (0x3f7020c5); at 0.5 s it fades to 1 over 863 ms (mode 3) while it scales to 1 (mode 4,
// 863 ms); at 1.6 s it fades to 0 over 174 ms (mode 1) while it scales to 1.28402 (0x3fa45a1d, 174 ms). The modes are
// run on View::PafEase's two curves (mode 4's table, every other mode's reciprocal ramp): the RCO's accelMode is not
// decoded further than that, so the curve shapes are INFERRED; the times and end values are TRACED.
GameBoot::Logo GameBoot::logoAt(double t) {
    View::PafEase fade,scale;
    fade.retarget(0);scale.retarget(bits(0x3f7020c5u));
    if(t>=fadeInAt) {
        fade.retarget(1,fadeInMs,3);scale.retarget(1,fadeInMs,4);
        const double first=std::min(t,fadeOutAt)-fadeInAt;
        fade.advance(first);scale.advance(first);
    }
    if(t>=fadeOutAt) {
        fade.retarget(0,fadeOutMs,1);scale.retarget(bits(0x3fa45a1du),fadeOutMs,1);
        fade.advance(t-fadeOutAt);scale.advance(t-fadeOutAt);
    }
    return {std::clamp(fade.value,0.,1.),scale.value};
}

// FUN_000c3bb4 (vsh.elf) runs every frame: with the Auto-Off setting S seconds and the time since the last input E
// (both against the timebase), it raises system_plugin's page_autooff_guide text msg_help_system_off_soon_by_setting
// once S - E <= 600 s, and hides it again when input pushes S - E back over 600 s.
bool GameBoot::guideDue(qint64 idleMs,int minutes) {
    return minutes>0 && double(idleMs)>=minutes*60000.-guideWindowSeconds*1000.;
}

void GameBoot::attach(Assets& assets) { gameBoot().assets=&assets; }
bool GameBoot::active() { return gameBoot().active; }
double GameBoot::seconds() { auto& s=gameBoot();return s.active?(gameBootPinned()>=0?gameBootPinned():s.clock.nsecsElapsed()/1e9):-1; }

void GameBoot::begin(std::function<void()> launch) {
    auto& s=gameBoot();
    if(gameBootDisabled() || s.active || !s.assets) {
        if(launch)launch();
        return;
    }
    s.active=true;s.launch=std::move(launch);s.clock.start();
    const quint64 generation=++s.generation;
    qInfo().noquote()<<"gameboot: anim_gameboot started (the title launches when it ends or on a press after 0.4 s)";
    if(gameBootPinned()>=0){qInfo().noquote()<<QString("gameboot: held at %1 s (XMB_GAMEBOOT_AT)").arg(gameBootPinned(),0,'f',3);return;}
    QTimer::singleShot(int(finished*1000),qApp,[generation]{
        auto& state=gameBoot();
        if(!state.active || state.generation!=generation)return;
        qInfo().noquote()<<"gameboot: Finished event at 2.8 s; launching";
        state.active=false;auto go=std::move(state.launch);state.launch=nullptr;if(go)go();
    });
}

void GameBoot::skip() {
    auto& s=gameBoot();
    if(!s.active || gameBootPinned()>=0 || s.clock.nsecsElapsed()<400000000)return;
    qInfo().noquote()<<QString("gameboot: skipped at %1 s; launching").arg(s.clock.nsecsElapsed()/1e9,0,'f',3);
    s.active=false;auto go=std::move(s.launch);s.launch=nullptr;if(go)go();
}

void GameBoot::setAutoOff(qint64 idleMs,int minutes,bool allowed,const QString& fontFamily) {
    auto& s=gameBoot();
    s.idleMs=idleMs;s.minutes=minutes;s.allowed=allowed;s.family=fontFamily;
}

void GameBoot::render(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight,GLuint targetFbo) {
    auto& s=gameBoot();
    if(pixelWidth<=0 || pixelHeight<=0 || !s.assets)return;
    const bool boot=s.active;
    const double guide=boot?0.:s.guideAmount();
    if(!boot && guide<=.002)return;
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,pixelWidth,pixelHeight);
    QOpenGLPaintDevice device(QSize(pixelWidth,pixelHeight));
    QPainter painter(&device);
    painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
    if(boot) {
        // game_plugin page_gameboot_bg: ps3logo_back_color, a black plane at alpha 1 (INFERRED to sit under the logo,
        // as gamelib_plugin's page does); custom_render_plugin's ps3logo plane is the RCO's tex_ps3logo, centred.
        painter.fillRect(QRect(0,0,pixelWidth,pixelHeight),Qt::black);
        painter.setTransform(DesignSpace(pixelWidth,pixelHeight).transform());
        const Logo logo=logoAt(gameBootPinned()>=0?gameBootPinned():s.clock.nsecsElapsed()/1e9);
        const QImage& image=s.assets->image(QStringLiteral("custom_render_plugin"),QStringLiteral("tex_ps3logo"));
        if(!image.isNull() && logo.alpha>0) {
            painter.setOpacity(logo.alpha);
            painter.translate(960,540);painter.scale(logo.scale,logo.scale);
            painter.drawImage(QPointF(-image.width()/2.,-image.height()/2.),image);
        }
        painter.end();
        return;
    }
    // The guide: the operation-guide plate (tex_operation_guide_base over its shadow, 12 px corners) at the style-4
    // anchor the Options hint uses (right edge 1816, bottom 995), growing left with the text and up with extra lines;
    // shown at once, hidden over 200 ms (the show is FUN_00345b08 with a 0 s fade; the hide curve is not traced, linear).
    // It is painted once into a raster image at the output's scale (Qt's GL text path garbles 10 px glyphs) and drawn
    // 1:1 with the amount as opacity.
    const DesignSpace design(pixelWidth,pixelHeight);
    const double scale=design.y;
    const QString text=s.assets->text(QStringLiteral("system_plugin"),QStringLiteral("msg_help_system_off_soon_by_setting"),
        QStringLiteral("The system will turn off soon because the [System Auto-Off] setting is enabled.\n"
                       "This setting can be adjusted under [Settings] > [Power Save Settings]."));
    // The string is two paragraphs (a newline in the RCO). The plate keeps the one-line plate's bottom edge and right
    // anchor and grows up by one line pitch per extra line and left to the widest line (INFERRED: the widget's text
    // layout is not traced beyond the one-line Options hint).
    QFont font(s.family);font.setPixelSize(21);
    const QFontMetricsF metrics(font);
    const QStringList lines=text.trimmed().split(QLatin1Char('\n'));
    double advance=0;
    for(const QString& line:lines)advance=std::max(advance,metrics.horizontalAdvance(line.trimmed()));
    const double plateRight=1816.,bottom=995.,pitch=metrics.lineSpacing();
    const double height=41.+pitch*double(lines.size()-1),top=bottom-height;
    const double textLeft=plateRight-22.-advance,plateLeft=textLeft-21.;
    const QRectF plate(plateLeft,top,plateRight-plateLeft,height);
    const QRectF cover=plate.adjusted(-1.,-1.,1.,1.);   // the shadow plate is 2 px larger
    const QString key=text+QLatin1Char('|')+s.family+QLatin1Char('|')+QString::number(scale,'f',4);
    if(s.guideKey!=key) {
        s.guideKey=key;
        s.guideImage=QImage(QSize(int(std::ceil(cover.width()*scale)),int(std::ceil(cover.height()*scale))),QImage::Format_ARGB32_Premultiplied);
        s.guideImage.fill(Qt::transparent);
        QPainter raster(&s.guideImage);
        raster.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
        raster.scale(scale,scale);raster.translate(-cover.topLeft());
        const auto nineSlice=[&](const char* name,const QRectF& rect){
            const QImage& image=s.assets->image(QStringLiteral("system_plugin"),QLatin1String(name));
            if(image.isNull())return;
            const qreal i=std::min<qreal>(12.,std::min(image.width(),image.height())/2.);
            const qreal sx[4]={0,i,qreal(image.width())-i,qreal(image.width())};
            const qreal sy[4]={0,i,qreal(image.height())-i,qreal(image.height())};
            const qreal dx[4]={rect.left(),rect.left()+i,rect.right()-i,rect.right()};
            const qreal dy[4]={rect.top(),rect.top()+i,rect.bottom()-i,rect.bottom()};
            for(int row=0;row<3;++row)for(int col=0;col<3;++col)
                raster.drawImage(QRectF(dx[col],dy[row],dx[col+1]-dx[col],dy[row+1]-dy[row]),image,
                                 QRectF(sx[col],sy[row],sx[col+1]-sx[col],sy[row+1]-sy[row]));
        };
        nineSlice("tex_operation_guide_base_shadow",cover);
        nineSlice("tex_operation_guide_base",plate);
        raster.setFont(font);raster.setPen(Qt::white);
        for(int i=0;i<lines.size();++i)
            raster.drawText(QRectF(textLeft,top+(height-pitch*double(lines.size()))/2.+pitch*i,advance+6.,pitch),
                            Qt::AlignLeft|Qt::AlignVCenter,lines[i].trimmed());
    }
    painter.setOpacity(guide);
    painter.drawImage(QRectF(std::round(design.left+cover.left()*design.x),std::round(design.top+cover.top()*design.y),
                            s.guideImage.width()*design.x/design.y,s.guideImage.height()),s.guideImage);
    painter.end();
}
}
