#include "ribbon.h"
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QtEndian>
#include <cstring>

namespace xmb {
namespace {
constexpr float thinness=0.f;       // shader value; the presets say THINNESS 1, which the plugin remaps (not traced): 0 = full stripe period
constexpr QVector3D colour3{.847347f,.846155f,.846448f},colour4{.92487f,.922603f,.922933f}; // BACKGROUND.mnu 3/4: line gradient
// Camera of the spline task's matrix (captured at 0x20401dc0 and decomposed): fovy 53, near .1, far 1000.
constexpr float cameraFovy=53.f,cameraNear=.1f,cameraFar=1000.f;
constexpr float particleFovy=71.846f; // particles.cpp projection, for the emitter hand-off
constexpr float pi=3.14159265358979f;

QHash<QString,float> readMnu(const QByteArray& text) {
    QHash<QString,float> values;
    for(const auto& raw:text.split('\n')) {
        const auto parts=QString::fromLatin1(raw).trimmed().split(':');
        if(parts.size()==3 && (parts[1]=="float" || parts[1]=="int"))values.insert(parts[0],parts[2].toFloat());
    }
    return values;
}
Ribbon::Preset readPreset(const QByteArray& text,Ribbon::Preset preset) {
    const auto v=readMnu(text);
    const auto get=[&](const char* name,float& out){if(v.contains(name))out=v.value(name);};
    const auto get3=[&](const char* x,const char* y,const char* z,QVector3D& out){
        if(v.contains(x) && v.contains(y) && v.contains(z))out=QVector3D(v.value(x),v.value(y),v.value(z));
    };
    get("DAMPING",preset.damping);get("LENGTH",preset.length);get("TENSION",preset.tension);get("TIMESTEP",preset.timestep);
    get("PERTURBATION",preset.perturbation);get("END Y",preset.endY);get("END Z",preset.endZ);get("FFD PARAM 1",preset.ffdParam);
    get3("FFD SCALE1 X","FFD SCALE1 Y","FFD SCALE1 Z",preset.scale1);get3("FFD SCALE2 X","FFD SCALE2 Y","FFD SCALE2 Z",preset.scale2);
    get3("FFD OFFSET X","FFD OFFSET Y","FFD OFFSET Z",preset.offset);get3("POS X","POS Y","POS Z",preset.pos);get3("ANG X","ANG Y","ANG Z",preset.axis);
    get("ANG ROT",preset.angle);get("SPACING",preset.spacing);get("BRIGHTNESS",preset.brightness);get("MIPMAP BIAS",preset.mipmapBias);get("FRESNEL",preset.fresnel);
    return preset;
}
// Big-endian float table at a virtual address of a 64-bit PPU ELF (the decrypted plugin), or empty when out of range.
std::vector<float> elfFloats(const QByteArray& elf,quint64 address,int count) {
    if(elf.size()<0x40 || !elf.startsWith("\x7f" "ELF"))return {};
    const auto u16=[&](qsizetype at){return qFromBigEndian<quint16>(elf.constData()+at);};
    const auto u32=[&](qsizetype at){return qFromBigEndian<quint32>(elf.constData()+at);};
    const auto u64=[&](qsizetype at){return qFromBigEndian<quint64>(elf.constData()+at);};
    const quint64 phoff=u64(0x20);const int entry=u16(0x36),entries=u16(0x38);
    for(int i=0;i<entries;++i) {
        const qsizetype h=qsizetype(phoff)+qsizetype(i)*entry;
        if(h+0x38>elf.size() || u32(h)!=1)continue;
        const quint64 offset=u64(h+8),vaddr=u64(h+16),size=u64(h+32);
        if(address<vaddr || address+quint64(count)*4>vaddr+size || offset+size>quint64(elf.size()))continue;
        std::vector<float> out(static_cast<size_t>(count));
        for(int k=0;k<count;++k){const quint32 bits=u32(qsizetype(offset+address-vaddr)+k*4);std::memcpy(&out[size_t(k)],&bits,4);}
        return out;
    }
    return {};
}
// Uniform cubic B-spline basis and its derivative (spline.elf's matrix at LS 0xd5c0 and tables 0xd600/0xde00).
std::array<float,4> basis(float t){return {(1-t)*(1-t)*(1-t)/6,(3*t*t*t-6*t*t+4)/6,(-3*t*t*t+3*t*t+3*t+1)/6,t*t*t/6};}
std::array<float,4> basisDerivative(float t){return {-(1-t)*(1-t)/2,(3*t*t-4*t)/2,(-3*t*t+2*t+1)/2,t*t/2};}

std::unique_ptr<QOpenGLTexture> floatTexture(int width,int height,const std::vector<float>& rgba,QOpenGLTexture::Filter filter) {
    auto texture=std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture->setSize(width,height);texture->setFormat(QOpenGLTexture::RGBA32F);texture->allocateStorage();
    texture->setData(QOpenGLTexture::RGBA,QOpenGLTexture::Float32,rgba.data());
    texture->setMinificationFilter(filter);texture->setMagnificationFilter(filter);texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    return texture;
}
// Fullscreen triangle for the firmware fragment programs; v_tc[0] is the uv with y = 0 at the bottom.
const char* fullscreenVertex=R"(#version 330 core
out vec4 v_col0, v_col1, v_fog;
out vec4 v_tc[10];
void main(){
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    for(int i=0;i<10;++i)v_tc[i]=vec4(0.);
    v_tc[0]=vec4(p,0.,1.);
    v_col0=v_col1=v_fog=vec4(0.);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
})";
}

Ribbon::Preset Ribbon::Preset::mix(const Preset& day,float t) const {
    const auto m=[&](float a,float b){return a+(b-a)*t;};
    Preset out;
    out.damping=m(damping,day.damping);out.length=m(length,day.length);out.tension=m(tension,day.tension);out.timestep=m(timestep,day.timestep);
    out.perturbation=m(perturbation,day.perturbation);out.endY=m(endY,day.endY);out.endZ=m(endZ,day.endZ);out.ffdParam=m(ffdParam,day.ffdParam);
    out.scale1=scale1+(day.scale1-scale1)*t;out.scale2=scale2+(day.scale2-scale2)*t;out.offset=offset+(day.offset-offset)*t;
    out.pos=pos+(day.pos-pos)*t;out.axis=axis+(day.axis-axis)*t;out.angle=m(angle,day.angle);
    out.spacing=m(spacing,day.spacing);out.brightness=m(brightness,day.brightness);out.mipmapBias=m(mipmapBias,day.mipmapBias);out.fresnel=m(fresnel,day.fresnel);
    return out;
}

// FUN_0004ab04: integer hash noise in (-1, 1], seeded by a counter (cloth +0x2d4) bumped before every call.
float Ribbon::Cloth::random() {
    ++seed;
    quint32 n=quint32(seed<<13)^quint32(seed);
    n=n*(n*n*15731u+789221u)+1376312589u;
    return 1.f-float(n&0x7fffffffu)*9.313225746154785e-10f;
}
void Ribbon::Cloth::spring(int i,int j,float rest,float k) {
    const QVector3D d=pos[size_t(j)].toVector3D()-pos[size_t(i)].toVector3D();
    const float length=d.length();
    if(length<=0)return;
    const QVector4D force(d/length*((length-rest)*k),0);
    vel[size_t(i)]+=force;vel[size_t(j)]-=force;
}
// FUN_0004bed0 + FUN_0004bce0: one fixed step. Spring impulses (structural to the left/upper neighbour, bending to
// the second neighbour at 10x tension) and a random kick go straight into the velocities, then explicit Euler
// with dt = TIMESTEP x 1e-4 and damping DAMPING x TIMESTEP. The last column is driven: x = 0, y and z follow
// sin(11t) and cos(15t) with a per-row phase; every row's first point is pulled along +x.
void Ribbon::Cloth::step(const Preset& p) {
    prev=pos;
    target+=p.timestep*1e-4f;
    if(target>10)target=0;
    const float k=p.tension,bend=p.tension*10;
    for(int r=0;r<side_;++r)for(int c=0;c<side_;++c) {
        const int i=r*side_+c;
        if(c>=1)spring(i,i-1,p.length,k);
        if(c>=2)spring(i,i-2,p.length*2,bend);
        if(r>=1)spring(i,i-side_,p.length,k);
        if(r>=2)spring(i,i-2*side_,p.length*2,bend);
        const float x=random(),y=random(),z=random();
        vel[size_t(i)]+=QVector4D(x,y,z,0)*p.perturbation;
    }
    const float dt=p.timestep*1e-4f,damping=p.damping*p.timestep;
    for(int i=0;i<points_;++i){pos[size_t(i)]+=vel[size_t(i)]*dt;vel[size_t(i)]-=vel[size_t(i)]*damping;}
    phase+=(target-phase)*.1f;
    for(int r=0;r<side_;++r) {
        const float t=float(r)/side_+phase;
        vel[size_t(r*side_)]+=QVector4D(.02f*p.timestep,0,0,0);
        const size_t end=size_t((r+1)*side_-1);
        pos[end]=QVector4D(0,(std::sin(11*t)*.5f+.5f)*p.endY,(std::cos(15*t)+1)*.5f*p.endZ,1);
        vel[end]=QVector4D();
    }
}

// FUN_0004f814, after the steps: the drawn lattice is the previous and current states blended by the leftover step
// fraction, its y and z scaled by 1.3 - cos(d pi/2) (1 - smoothstep(d/5)), d = x - FFD PARAM 1 (0.3 where d < 0).
void Ribbon::Cloth::swell(const Preset& p) {
    for(int i=0;i<points_;++i) {
        const QVector4D l=prev[size_t(i)]*(1-acc)+pos[size_t(i)]*acc;
        const float d=l.x()-p.ffdParam,s=std::clamp(d/5,0.f,1.f);
        const float scale=1.3f-(d<0?1.f:std::cos(d*pi/2))*(1-s)*(1-s)*(1+2*s);
        drawn[size_t(i)]=QVector4D(l.x(),l.y()*scale,l.z()*scale,1);
    }
}

bool Ribbon::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    const auto load=[&](const char* name,CgProgram& program) {
        QString error;
        try { program=CgProgram::load(assets.qglFile("lines",QString("lib/moyou/")+name),&error); }
        catch(const std::exception& e) { error=e.what(); }
        if(!program.valid())qWarning().noquote()<<"ribbon:"<<name<<"unavailable:"<<error;
        return program.valid();
    };
    if(!load("ffd_shader1.fpo",ffdCg_) || !load("lines1.vpo",linesVertexCg_) || !load("lines1.fpo",linesFragmentCg_))return false;
    if(!ffd_.addShaderFromSourceCode(QOpenGLShader::Vertex,fullscreenVertex) || !ffd_.addShaderFromSourceCode(QOpenGLShader::Fragment,ffdCg_.glsl) || !ffd_.link()) {
        qWarning().noquote()<<"ribbon: ffd_shader1 failed:"<<ffd_.log();return false;
    }
    // The stripe coordinate is fract(u x SPACING); its screen derivatives jump at every wrap, so plain texture() picks
    // a wrong mip there and leaves dark dashes in the band. Sample with the gradients of the unwrapped coordinate
    // instead (the RSX's quad-based LOD hid this). Only that one fetch changes.
    QString fragment=linesFragmentCg_.glsl;
    const QString stripeFetch="texture(tex0, (h[0]).xy)";
    if(fragment.count(stripeFetch)==1)
        fragment.replace(stripeFetch,"textureGrad(tex0, (h[0]).xy, vec2(dFdx(v_tc[0].x*fc[1].x), dFdx(v_tc[0].y)), vec2(dFdy(v_tc[0].x*fc[1].x), dFdy(v_tc[0].y)))");
    else qWarning()<<"ribbon: stripe fetch not found in lines1.fpo translation; mip seams stay";
    if(!lines_.addShaderFromSourceCode(QOpenGLShader::Vertex,linesVertexCg_.glsl) || !lines_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !lines_.link()) {
        qWarning().noquote()<<"ribbon: lines1 failed:"<<lines_.log();return false;
    }
    // LinesController.fpo, reduced: the summed line intensity times the horizontal gradient of BACKGROUND.mnu colours
    // 3 and 4. The firmware also multiplies the background by colours 1 and 2 (both white) and pre-exposes for HDR.
    static const char* composite=R"(#version 330 core
in vec4 v_tc[10];
layout(location=0) out vec4 colour;
uniform sampler2D sum;uniform vec3 colour3,colour4;
void main(){
    float l=texture(sum,v_tc[0].xy).r;
    colour=vec4(l*mix(colour3,colour4,v_tc[0].x),1.0);
})";
    if(!composite_.addShaderFromSourceCode(QOpenGLShader::Vertex,fullscreenVertex) || !composite_.addShaderFromSourceCode(QOpenGLShader::Fragment,composite) || !composite_.link()) {
        qWarning().noquote()<<"ribbon: composite failed:"<<composite_.log();return false;
    }
    // Presets: LINE1.mnu of the overrides the plugin schedules (boot, then the clock keyframes). A missing file keeps
    // the day values, or night's for night.
    const Preset defaults;
    const auto preset=[&](const char* name,const Preset& fallback){return readPreset(assets.qglFile("lines",QString("override/%1/LINE1.mnu").arg(name)),fallback);};
    day_=preset("day",defaults);
    Preset nightDefaults=defaults;nightDefaults.spacing=408.298f;nightDefaults.brightness=.9f;nightDefaults.mipmapBias=1.75802f;
    nightDefaults.fresnel=.52f;nightDefaults.perturbation=.1f;nightDefaults.angle=10;nightDefaults.scale2={3.2f,1.2f,3};nightDefaults.ffdParam=-1.83395f;
    night_=preset("night",nightDefaults);
    yoake_=preset("yoake",day_);higure_=preset("higure",night_);
    coldboot1_=preset("coldboot1",day_);coldboot2_=preset("coldboot2",day_);
    current_=coldboot1_;
    // _UVW (FUN_000493c4): the 8x4x4 FFD node grid, (x/8, y/4, z/4, 0) at texel z*32 + y*8 + x; ffd_shader1 renders
    // it into a 128x1 float target that the plugin reads back.
    std::vector<float> uvw(128*4);
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<8;++x){float* p=&uvw[size_t((z*32+y*8+x)*4)];p[0]=x*.125f;p[1]=y*.25f;p[2]=z*.25f;p[3]=0;}
    uvw_=floatTexture(128,1,uvw,QOpenGLTexture::Nearest);
    QOpenGLFramebufferObjectFormat fieldFormat;fieldFormat.setInternalTextureFormat(GL_RGBA32F);
    field_=std::make_unique<QOpenGLFramebufferObject>(128,1,fieldFormat);
    // The cloth's rest state: the plugin starts the boot ribbon from preset 1 (FUN_0004e624: positions at 0x97c18,
    // velocities at 0x98d04, 361 x float3 each, read from the local decrypted plugin). Without it, an approximation of
    // that state settles for four minutes of simulated time: the tables form a tube (row 0 equals row 18, each column closes
    // into a loop), columns LENGTH apart from the pinned edge at x = 0 and rows round a loop of the tables' y/z extent.
    const QByteArray plugin=assets.vshModule("custom_render_plugin.prx");
    const auto rest=elfFloats(plugin,0x97c18,points_*3),restVelocity=elfFloats(plugin,0x98d04,points_*3);
    const bool fromTables=rest.size()==size_t(points_*3) && restVelocity.size()==size_t(points_*3);
    for(int i=0;i<points_;++i) {
        const size_t k=size_t(i)*3;
        cloth_.pos[size_t(i)]=fromTables?QVector4D(rest[k],rest[k+1],rest[k+2],1):QVector4D(float(side_-1-i%side_)*day_.length,.35f+.55f*std::sin(2*pi*float(i/side_)/(side_-1)),.55f+.55f*std::cos(2*pi*float(i/side_)/(side_-1)),1);
        cloth_.vel[size_t(i)]=fromTables?QVector4D(restVelocity[k],restVelocity[k+1],restVelocity[k+2],0):QVector4D();
    }
    cloth_.prev=cloth_.pos;
    if(!fromTables)for(int i=0;i<14400;++i)cloth_.step(day_); // the x extent crumples first, then recovers (cloth-sim.ts)
    // _Stripes (FUN_0004bad0): row k of 4, x in [-.5,.5]: min(1, pow(1.01 - 4x^2, 20 (k/3)^3) * (k/6 + .5)).
    constexpr int stripeWidth=512; // the plugin's width is a runtime global; 512 resolves the profile
    std::vector<float> stripes(size_t(stripeWidth)*4*4);
    for(int k=0;k<4;++k)for(int i=0;i<stripeWidth;++i) {
        const float f=k/3.f,x=float(i)/(stripeWidth-1)-.5f;
        const float v=std::min(1.f,std::pow(std::max(0.f,x*-4.f*x+1.01f),f*20.f*f*f)*(f*.5f+.5f));
        float* p=&stripes[(size_t(k)*stripeWidth+i)*4];p[0]=p[1]=p[2]=p[3]=v;
    }
    // Mipmapped with the preset's MIPMAP BIAS as LOD bias: across the sheet the ~400 stripes fall below a pixel and
    // the mip chain averages them into the translucent body of the ribbon (level 0 alone aliases into moiré).
    stripes_=std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    stripes_->setSize(stripeWidth,4);stripes_->setFormat(QOpenGLTexture::RGBA32F);stripes_->setMipLevels(stripes_->maximumMipLevels());
    stripes_->allocateStorage();stripes_->setData(0,QOpenGLTexture::RGBA,QOpenGLTexture::Float32,stripes.data());
    stripes_->generateMipMaps();
    stripes_->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,QOpenGLTexture::Linear);
    stripes_->setWrapMode(QOpenGLTexture::ClampToEdge);stripes_->setLevelofDetailBias(day_.mipmapBias);
    // _Encode (FUN_0004b928) splits v*128 into fraction/4 and integer/64 so that LinesController can sum
    // RGBA8 additively (decode R/32 + G/2 = v). A float target needs no split: the identity ramp, capped at 1 as there.
    std::vector<float> encode(4096*4);
    for(int i=0;i<4096;++i){float* p=&encode[size_t(i)*4];p[0]=p[2]=float(i)/4095;p[1]=p[3]=0;}
    encode_=floatTexture(4096,1,encode,QOpenGLTexture::Linear);
    QImage lut;
    try { lut=Assets::decodeTga(assets.qglFile("lines","textures/TGA/freslut1.tga")); } catch(const std::exception& e) { qWarning()<<"ribbon:"<<e.what(); }
    if(lut.isNull()){qWarning()<<"ribbon: freslut1.tga could not be decoded";return false;}
    fresLut_=std::make_unique<QOpenGLTexture>(lut); // RSX images are stored top row first
    fresLut_->setMinificationFilter(QOpenGLTexture::Linear);fresLut_->setMagnificationFilter(QOpenGLTexture::Linear);fresLut_->setWrapMode(QOpenGLTexture::ClampToEdge);
    // Sheet: 128 x 128 vertices as spline.elf writes them (row r across the ribbon, column c along it), attributes
    // in_0 (clip position), in_8 (r/127 and the edge fade, from local memory 0xc2bb4100) and in_9 (normal).
    // The firmware draws 17-column triangle strips with restarts; a triangle list covers the same quads.
    std::vector<GLuint> indices;
    for(int r=0;r+1<grid_;++r)for(int c=0;c+1<grid_;++c) {
        const GLuint a=GLuint(r*grid_+c),b=a+1,d=a+grid_,e=d+1;
        indices.insert(indices.end(),{a,d,b,b,d,e});
    }
    indexCount_=GLsizei(indices.size());
    vertices_.assign(size_t(grid_)*grid_*12,0.f);
    for(int r=0;r<grid_;++r)for(int c=0;c<grid_;++c) {
        // FUN_0004d190: min(1, (.5 - |.5 - s|) * 10) for both sheet coordinates, multiplied.
        const float u=float(r)/(grid_-1),w=float(c)/(grid_-1);
        float* v=&vertices_[(size_t(r)*grid_+c)*12];
        v[4]=u;v[5]=std::min(1.f,(.5f-std::abs(.5f-w))*10.f)*std::min(1.f,(.5f-std::abs(.5f-u))*10.f);
    }
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);gl.glGenBuffers(1,&ibo_);gl.glGenVertexArrays(1,&emptyVao_);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);gl.glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices_.size()*sizeof(float)),nullptr,GL_STREAM_DRAW);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_);gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(indices.size()*sizeof(GLuint)),indices.data(),GL_STATIC_DRAW);
    for(int location:{0,8,9}) {
        gl.glEnableVertexAttribArray(GLuint(location));
        gl.glVertexAttribPointer(GLuint(location),4,GL_FLOAT,GL_FALSE,12*sizeof(float),reinterpret_cast<void*>(size_t(location==0?0:location==8?4:8)*sizeof(float)));
    }
    gl.glBindVertexArray(0);
    // Local verification: XMB_RIBBON_FFD_SWEEP=<file> writes the day deformation lattice for _Time 0..100 in 0.01
    // steps (time, then 539 x xyz), to match against captured spline task inputs (work/spu/check-ffd-sweep.ts).
    if(qEnvironmentVariableIsSet("XMB_RIBBON_FFD_SWEEP")) {
        QFile out(qEnvironmentVariable("XMB_RIBBON_FFD_SWEEP"));
        if(out.open(QIODevice::WriteOnly)) {
            const float saved=cloth_.target;
            for(int k=0;k<=10000;++k) {
                cloth_.target=k*.001f;deformLattice(gl,day_);
                QByteArray row=QByteArray::number(cloth_.target*10,'g',9);
                for(const auto& d:deform_)row+=' '+QByteArray::number(d.x(),'g',9)+' '+QByteArray::number(d.y(),'g',9)+' '+QByteArray::number(d.z(),'g',9);
                out.write(row+'\n');
            }
            cloth_.target=saved;
        }
    }
    ready_=true;
    qInfo()<<"ribbon: ffd_shader1"<<ffdCg_.instructionCount<<"+ lines1"<<linesVertexCg_.instructionCount<<"/"<<linesFragmentCg_.instructionCount
           <<"instructions, FresLUT"<<lut.size()<<(fromTables?"cloth from plugin preset 1":"cloth from a settled flat grid (no --vsh-modules)");
    return true;
}

// FUN_0004a220 / FUN_00047b1c: ffd_shader1 over the node grid at _Time = 10 x the cloth's wave clock, read back,
// scaled by FFD SCALE1 plus FFD OFFSET, and padded to 11x7x7 (index clamp(x-1,0,7), clamp(y-1,0,3), clamp(z-1,0,3)).
void Ribbon::deformLattice(QOpenGLFunctions_3_3_Core& gl,const Preset& p) {
    QVector<QVector4D> fc=ffdCg_.constants;
    for(int slot:ffdCg_.parameters.value("_Time").constantSlots)if(slot<fc.size())fc[slot]=QVector4D(1,1,1,1)*(cloth_.target*10);
    field_->bind();gl.glViewport(0,0,128,1);gl.glDisable(GL_BLEND);
    ffd_.bind();ffd_.setUniformValueArray("fc",fc.constData(),fc.size());ffd_.setUniformValue("tex0",0);uvw_->bind(0);
    gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);
    gl.glReadPixels(0,0,128,1,GL_RGBA,GL_FLOAT,fieldPixels_.data());
    uvw_->release(0);ffd_.release();
    for(int z=0;z<7;++z)for(int y=0;y<7;++y)for(int x=0;x<11;++x) {
        const int node=(std::clamp(z-1,0,3)*4+std::clamp(y-1,0,3))*8+std::clamp(x-1,0,7);
        const float* f=&fieldPixels_[size_t(node)*4];
        deform_[size_t(z*77+y*11+x)]=QVector4D(QVector3D(f[0],f[1],f[2])*p.scale1+p.offset,1);
    }
}

// spline.elf: FFD of every cloth point (func_3c68), the camera (func_3bf0 with the matrix the plugin builds from
// POS/ANG), then the uniform bicubic B-spline sheet (func_45c0): 16 x 16 patches of 8 x 8 samples at
// t = k x 16/127, positions and normal = dP/dv x dP/du in clip space.
void Ribbon::splineSheet(const Preset& p,float aspect) {
    QMatrix4x4 rotation;rotation.rotate(p.angle,p.axis);
    const float sy=1.f/std::tan(cameraFovy*pi/360),sx=sy/aspect;
    const float za=(cameraFar+cameraNear)/(cameraNear-cameraFar),zb=2*cameraFar*cameraNear/(cameraNear-cameraFar);
    const QVector3D eye=p.pos+QVector3D(0,0,-2);
    for(int i=0;i<points_;++i) {
        const QVector3D point=cloth_.lattice(i);
        const QVector3D n=(point-p.offset)/p.scale1;
        int cell[3];std::array<float,4> w[3];
        for(int a=0;a<3;++a){const float s=std::clamp(n[a],0.f,.999f)*(a==0?8.f:4.f);cell[a]=int(s);w[a]=basis(s-float(cell[a]));}
        QVector3D sum;
        for(int a=0;a<4;++a)for(int b=0;b<4;++b)for(int c=0;c<4;++c)
            sum+=deform_[size_t((cell[0]+a)+11*(cell[1]+b)+77*(cell[2]+c))].toVector3D()*(w[0][size_t(a)]*w[1][size_t(b)]*w[2][size_t(c)]);
        const QVector3D view=rotation.map((n+sum)*p.scale2)+eye;
        clipLattice_[size_t(i)]=QVector4D(view.x()*sx,view.y()*sy,view.z()*za+zb,-view.z());
    }
    std::array<int,grid_> segment{};std::array<std::array<float,4>,grid_> value{},slope{};
    for(int k=0;k<grid_;++k){segment[size_t(k)]=k/8;const float t=float(k%8)*16/127.f;value[size_t(k)]=basis(t);slope[size_t(k)]=basisDerivative(t);}
    for(int r=0;r<grid_;++r)for(int c=0;c<grid_;++c) {
        QVector4D position,du,dv;
        const auto &bv=value[size_t(r)],&dvw=slope[size_t(r)],&bu=value[size_t(c)],&duw=slope[size_t(c)];
        for(int a=0;a<4;++a)for(int b=0;b<4;++b) {
            const QVector4D& q=clipLattice_[size_t((segment[size_t(r)]+a)*side_+segment[size_t(c)]+b)];
            position+=q*(bv[size_t(a)]*bu[size_t(b)]);du+=q*(bv[size_t(a)]*duw[size_t(b)]);dv+=q*(dvw[size_t(a)]*bu[size_t(b)]);
        }
        const QVector3D normal=QVector3D::crossProduct(dv.toVector3D(),du.toVector3D());
        float* v=&vertices_[(size_t(r)*grid_+c)*12];
        v[0]=position.x();v[1]=position.y();v[2]=position.z();v[3]=position.w();
        v[8]=normal.x();v[9]=normal.y();v[10]=normal.z();v[11]=0;
    }
}

// FUN_00011600: the clock keyframes at 0x9c98c, (fraction of the day, preset), blended linearly between neighbours:
// night to 01:00, to yoake by 05:00, held to 07:00, to day by 11:00, held to 13:00, to higure by 17:00, held to
// 19:00, to night by 23:00.
Ribbon::Preset Ribbon::clockPreset(double dayFraction) const {
    const struct { double at; const Preset* preset; } keys[]={{0,&night_},{1/24.,&night_},{5/24.,&yoake_},{7/24.,&yoake_},
        {11/24.,&day_},{13/24.,&day_},{17/24.,&higure_},{19/24.,&higure_},{23/24.,&night_},{1,&night_}};
    const double t=std::clamp(dayFraction,0.0,1.0);
    for(size_t i=1;i<std::size(keys);++i)
        if(t<keys[i].at)return keys[i-1].preset->mix(*keys[i].preset,float((t-keys[i-1].at)/(keys[i].at-keys[i-1].at)));
    return night_;
}

// The boot schedule (plugin 0x11100): coldboot1 at once, coldboot2 over 3 s, then the clock preset over 7.5 s. The
// blending runs in qgl_base's animation player; its curve here is measured from an RPCS3 cold boot (the spline task's
// FFD SCALE2, one sample per cloth step, audit/PIPELINE.md "Ribbon presets"):
//   coldboot1 -> coldboot2   value += (target - value) x 0.01 per 1/60 s until stage two
//   -> clock preset          from the values reached: 1.4e-5 n^2 for n <= 112 steps, then a cubic Hermite to 1 at
//                            n = 168 (2.8 s) with zero end slope
// after which the LINE1 values follow the clock keyframes. Stage two is anim_coldboot2's NormalBG2 event (0x11b20, at
// 4 s); RPCS3's cloth ran 229 steps by then (boot stalls), which is the ribbon's own default when no intro clock drives
// it (setBootTime).
Ribbon::BootWeights Ribbon::bootWeights(double t,double stageTwo) {
    BootWeights w;
    w.toColdboot2=float(1-std::pow(.99,std::min(t,stageTwo)*60));
    const double n=std::max(0.,t-stageTwo)*60;
    constexpr double knee=112,a=1.4e-5,kneeValue=a*knee*knee,kneeSlope=2*a*knee,span=168-knee;
    double p=a*n*n;
    if(n>=168)p=1;
    else if(n>knee) {
        const double s=(n-knee)/span,s2=s*s,s3=s2*s;
        p=(2*s3-3*s2+1)*kneeValue+(s3-2*s2+s)*span*kneeSlope+(-2*s3+3*s2);
    }
    w.toClock=float(p);
    return w;
}

void Ribbon::advancePresets(double dt,double dayFraction) {
    bootSeconds_=bootOverride_>=0?bootOverride_:bootSeconds_+dt;
    const BootWeights w=bootWeights(bootSeconds_,stageTwo_);
    current_=coldboot1_.mix(coldboot2_,w.toColdboot2).mix(clockPreset(dayFraction),w.toClock);
}

void Ribbon::render(QOpenGLFunctions_3_3_Core& gl,double seconds,double dayFraction,int pixelWidth,int pixelHeight,GLuint targetFbo) {
    if(!ready_ || pixelWidth<=0 || pixelHeight<=0)return;
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_SCISSOR_TEST);gl.glDisable(GL_CULL_FACE);
    // 1. The cloth (FUN_0004f814): 60 fixed steps per second, the drawn lattice interpolated between the last two.
    const double dt=lastSeconds_<0?1.0/60:std::clamp(seconds-lastSeconds_,0.0,.25);
    lastSeconds_=seconds;
    advancePresets(dt,dayFraction);
    const Preset p=current_;
    cloth_.acc+=float(dt*60);
    while(cloth_.acc>1){cloth_.step(p);cloth_.acc-=1;}
    cloth_.swell(p);
    // 2. The FFD lattice from ffd_shader1, 3. spline.elf's sheet.
    deformLattice(gl,p);
    const float aspect=float(pixelWidth)/float(pixelHeight);
    splineSheet(p,aspect);
    // Centre line (row 64) for the particles and the crest measurement.
    centreLine_.clear();
    const float particleTan=std::tan(particleFovy*pi/360);
    for(int c=0;c<grid_;++c) {
        const float* v=&vertices_[(size_t(grid_/2)*grid_+c)*12];
        if(v[3]<=0)continue;
        // Same screen position and depth under the particle camera: view x = ndc.x * depth * tan(fovy/2) * aspect.
        centreLine_.emplace_back(v[0]*particleTan*aspect,v[1]*particleTan,-v[3]);
    }
    std::sort(centreLine_.begin(),centreLine_.end(),[](const QVector3D& a,const QVector3D& b){return a.x()/-a.z()<b.x()/-b.z();});
    for(int k=0;k<int(crest_.size());++k) { // centre line, screen x -> fraction from the top
        const float sx=float(k)/(crest_.size()-1);float best=-1,bestDistance=1e9f;
        for(int c=0;c<grid_;++c) {
            const float* v=&vertices_[(size_t(grid_/2)*grid_+c)*12];
            if(v[3]<=0)continue;
            const float x=(v[0]/v[3]+1)*.5f,distance=std::abs(x-sx);
            if(distance<bestDistance){bestDistance=distance;best=(1-v[1]/v[3])*.5f;}
        }
        crest_[size_t(k)]=best;
    }
    // 4. lines1 into a float sum, additively (the firmware sums RGBA8 through _Encode).
    if(!sum_ || sum_->size()!=QSize(pixelWidth,pixelHeight)) {
        QOpenGLFramebufferObjectFormat format;format.setInternalTextureFormat(GL_RGBA16F);
        sum_=std::make_unique<QOpenGLFramebufferObject>(pixelWidth,pixelHeight,format);
    }
    sum_->bind();gl.glViewport(0,0,pixelWidth,pixelHeight);gl.glClearColor(0,0,0,0);gl.glClear(GL_COLOR_BUFFER_BIT);
    gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
    std::vector<QVector4D> vc(468);
    for(const auto& param:linesVertexCg_.parameters)if(param.resourceIndex>=0 && param.resourceIndex<468 && !param.defaults.isEmpty())
        vc[size_t(param.resourceIndex)]=QVector4D(param.defaults.value(0),param.defaults.value(1),param.defaults.value(2),param.defaults.value(3));
    const auto setVc=[&](const char* name,float value){const auto it=linesVertexCg_.parameters.constFind(name);if(it!=linesVertexCg_.parameters.cend() && it->resourceIndex>=0 && it->resourceIndex<468)vc[size_t(it->resourceIndex)]=QVector4D(value,value,value,value);};
    setVc("_Fresnel",p.fresnel);setVc("_Brightness",p.brightness);setVc("_MipmapBias",p.mipmapBias);
    QVector<QVector4D> lfc=linesFragmentCg_.constants;
    const auto setFc=[&](const char* name,float value){for(int slot:linesFragmentCg_.parameters.value(name).constantSlots)if(slot<lfc.size())lfc[slot]=QVector4D(value,value,value,value);};
    setFc("_Spacing",p.spacing);setFc("_Thinness",thinness);
    lines_.bind();
    lines_.setUniformValueArray("vc",vc.data(),int(vc.size()));lines_.setUniformValueArray("fc",lfc.constData(),lfc.size());
    lines_.setUniformValue("tex0",0);lines_.setUniformValue("tex1",1);lines_.setUniformValue("tex2",2);
    stripes_->setLevelofDetailBias(p.mipmapBias);
    stripes_->bind(0);fresLut_->bind(1);encode_->bind(2);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(vertices_.size()*sizeof(float)),vertices_.data());
    gl.glDrawElements(GL_TRIANGLES,indexCount_,GL_UNSIGNED_INT,nullptr);
    gl.glBindVertexArray(0);
    encode_->release(2);fresLut_->release(1);stripes_->release(0);lines_.release();
    // 5. Composite onto the target (LinesController: background + lines x gradient).
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,pixelWidth,pixelHeight);
    composite_.bind();composite_.setUniformValue("sum",0);composite_.setUniformValue("colour3",colour3);composite_.setUniformValue("colour4",colour4);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,sum_->texture());
    gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);gl.glBindVertexArray(0);
    gl.glBindTexture(GL_TEXTURE_2D,0);composite_.release();
    gl.glDisable(GL_BLEND);
}
}
