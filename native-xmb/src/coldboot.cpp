#include "coldboot.h"
#include <QAudioDecoder>
#include <QAudioSink>
#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QMediaDevices>
#include <QtEndian>
#include <QUrl>
#include <QtMath>
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
    skipped_=true;
    if(sink_)sink_->stop();
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
    if(!logoVisible() || !started_ || pixelWidth<=0 || pixelHeight<=0)return;
    std::array<QVector4D,3> colours;QVector3D lightPosition,lightColour,attenuation;
    state(seconds(),colours,lightPosition,lightColour,attenuation);
    QMatrix4x4 projection,view;
    projection.perspective(qRadiansToDegrees(yfov_),float(pixelWidth)/float(pixelHeight),znear_,zfar_);
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
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,pixelWidth,pixelHeight);
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
}
