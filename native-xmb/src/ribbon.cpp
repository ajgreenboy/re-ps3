#include "ribbon.h"
#include "particles.h"      // sceneMaterials(): the shared run-time reader
#include "rco_objects.h"
#include "idle_profile.h"
#include "uniform_cache.h"
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QtEndian>
#include <cstring>
#include <immintrin.h>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xmb {
namespace {
constexpr float thinness=0.f;       // fit: the shader value; the presets say THINNESS 1, which the plugin
                                    // remaps (not traced), and 0 = full stripe period matches the captures
// Camera of the spline task's matrix (captured at 0x20401dc0 and decomposed): fovy 53, near .1, far 1000.
// BACKGROUND.mnu's FOVY (71.846) is the background shader's camera, not this one.
constexpr float cameraFovy=53.f,cameraNear=.1f,cameraFar=1000.f;
constexpr float pi=3.14159265358979f;

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

// The cloth's fallback start, used only when the plugin's preset-1 tables are unavailable (no --vsh-modules).
// fit (measured 2026-10-28): columns LENGTH apart from the pinned edge at x = 0, rows round a y/z loop of the
// tables' own extent. It is NOT FUN_0004ec90: that function is the plugin's *other* fallback and builds a flat
// plane, x = (u - .5) x 4, y = (v - .5) x 4, z = -3 (its constants 4.0 / .5 / -3 / 1 at .data 0x92dc8, 0x92dc4,
// 0x92dcc, 0x92dd0). Settling from that traced plane draws the sheet 1031 px short at the right edge (x max 1368
// against the console's 2399, audit/PIPELINE.md "Ribbon extent"), so the loop below stays a fit.
// Shared by init's flat path and the prefetch, so the two cannot drift apart.
QVector4D Ribbon::flatPoint(int i,float length) {
    return QVector4D(float(side_-1-i%side_)*length,
                     .35f+.55f*std::sin(2*pi*float(i/side_)/(side_-1)),
                     .55f+.55f*std::cos(2*pi*float(i/side_)/(side_-1)),1);
}

// The cloth warm-up the no-plugin path always ran: 14,400 fixed steps from the flat start (the x extent crumples
// first, then recovers; work/cloth-sim.ts). A pure function of the preset - no GL, no member state - which is what
// lets it run on a worker thread. The timer is the measurement quoted in audit/VERIFICATION-HARNESS.md 5.6.
// measured 2026-10-28: the settle is a warm-up, not a rest state. Its end state is one phase of the swell, and the
// sheet it draws sits up to 95 px above and 204 px short of the console's; the console's own state is the plugin's
// preset-1 table, which reproduces it to 4 px (audit/PIPELINE.md "Ribbon extent"). Do not tune the step count, the
// start or the camera to a reference still: every value in that chain is traced and verified.
Ribbon::Settle Ribbon::settledFlat(const Preset& p) {
    Settle out;
    for(int i=0;i<points_;++i)out.cloth.pos[size_t(i)]=flatPoint(i,p.length);
    out.cloth.prev=out.cloth.pos;
    QElapsedTimer timer;timer.start();
    for(int i=0;i<14400;++i)out.cloth.step(p);
    out.micros=timer.nsecsElapsed()/1000;
    return out;
}

// The fields Cloth::step and flatPoint read. Exact comparison: a prefetched state may only replace the synchronous
// one when every input that shaped it is bit-identical, or the first frame would move.
bool Ribbon::sameClothPreset(const Preset& a,const Preset& b) {
    return a.damping==b.damping && a.length==b.length && a.tension==b.tension && a.timestep==b.timestep
        && a.perturbation==b.perturbation && a.endY==b.endY && a.endZ==b.endZ;
}

// Start the warm-up with the view. The first native window costs ~450 ms on this machine (VERIFICATION-HARNESS.md
// 5.4a), so the 14,400-step settle (~220 ms) finishes long before initializeGL asks for it. The thread touches only
// its own copy; the preset-1 table path (a --vsh-modules run) never consumes it, and the future's destructor joins
// the thread at shutdown. XMB_RIBBON_SETTLE_SYNC=1 keeps the pre-change synchronous path for the A/B.
Ribbon::Ribbon() {
    if(qEnvironmentVariableIsSet("XMB_RIBBON_SETTLE_SYNC"))return;
    try { settleFuture_=std::async(std::launch::async,[]{return settledFlat(Preset{});}); }
    catch(const std::exception& e) { qWarning().noquote()<<"ribbon: cloth warm-up thread unavailable:"<<e.what(); }
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
    // Presets: LINE1.mnu of the overrides the plugin schedules (boot, then the clock keyframes), read at
    // run time through the shared Materials reader. A missing file keeps the base values below, which
    // are LINE1.mnu's own day preset (the Preset initializers are that fallback, never the source).
    Materials& materials=sceneMaterials(assets);
    QElapsedTimer presetTimer;presetTimer.start();
    int presetFiles=0;
    const auto presetFrom=[&](const Materials::Mnu& mnu,const Preset& fallback){
        Preset preset=fallback;
        if(!mnu.ok)return preset;
        const auto get=[&](const char* name,float& out){out=float(mnu.number(QString::fromLatin1(name),out));};
        const auto get3=[&](const char* x,const char* y,const char* z,QVector3D& out){
            if(mnu.contains(QString::fromLatin1(x)) && mnu.contains(QString::fromLatin1(y)) && mnu.contains(QString::fromLatin1(z)))
                out=QVector3D(float(mnu.number(QString::fromLatin1(x))),float(mnu.number(QString::fromLatin1(y))),
                              float(mnu.number(QString::fromLatin1(z))));
        };
        get("DAMPING",preset.damping);get("LENGTH",preset.length);get("TENSION",preset.tension);get("TIMESTEP",preset.timestep);
        get("PERTURBATION",preset.perturbation);get("END Y",preset.endY);get("END Z",preset.endZ);get("FFD PARAM 1",preset.ffdParam);
        get3("FFD SCALE1 X","FFD SCALE1 Y","FFD SCALE1 Z",preset.scale1);get3("FFD SCALE2 X","FFD SCALE2 Y","FFD SCALE2 Z",preset.scale2);
        get3("FFD OFFSET X","FFD OFFSET Y","FFD OFFSET Z",preset.offset);get3("POS X","POS Y","POS Z",preset.pos);
        get3("ANG X","ANG Y","ANG Z",preset.axis);
        get("ANG ROT",preset.angle);get("SPACING",preset.spacing);get("BRIGHTNESS",preset.brightness);
        get("MIPMAP BIAS",preset.mipmapBias);get("FRESNEL",preset.fresnel);
        return preset;
    };
    const Preset defaults;
    const Materials::Mnu& dayMnu=materials.line1(QStringLiteral("day"));++presetFiles;
    day_=presetFrom(dayMnu,defaults);
    // A missing override inherits the clock-day preset (as a preset without a LINE1.mnu inherits the
    // base in the firmware), rather than a second hardcoded table.
    night_=presetFrom(materials.line1(QStringLiteral("night")),day_);++presetFiles;
    yoake_=presetFrom(materials.line1(QStringLiteral("yoake")),day_);++presetFiles;
    higure_=presetFrom(materials.line1(QStringLiteral("higure")),day_);++presetFiles;
    coldboot1_=presetFrom(materials.line1(QStringLiteral("coldboot1")),day_);++presetFiles;
    coldboot2_=presetFrom(materials.line1(QStringLiteral("coldboot2")),day_);++presetFiles;
    current_=coldboot1_;
    // BACKGROUND.mnu keys 3/4: the line gradient LinesController multiplies the summed lines by. The
    // values are identical in the base file and every preset the plugin schedules for this layer
    // (day/night/yoake/higure/coldboot1/coldboot2); black/music_1/welcome change them and this build
    // never schedules those as a background, so the base read is the running value.
    {
        const Materials::Mnu& background=materials.background();++presetFiles;
        if(background.ok) {
            colour3_=QVector3D(float(background.number(QStringLiteral("3 RED"),colour3_.x())),
                               float(background.number(QStringLiteral("3 GREEN"),colour3_.y())),
                               float(background.number(QStringLiteral("3 BLUE"),colour3_.z())));
            colour4_=QVector3D(float(background.number(QStringLiteral("4 RED"),colour4_.x())),
                               float(background.number(QStringLiteral("4 GREEN"),colour4_.y())),
                               float(background.number(QStringLiteral("4 BLUE"),colour4_.z())));
        } else {
            qWarning().noquote()<<"ribbon: BACKGROUND.mnu unavailable ("<<background.source()<<background.error
                               <<"); using the compiled fallback line gradient";
        }
    }
    sceneNoteMaterials(presetFiles,presetTimer.nsecsElapsed());
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
        cloth_.pos[size_t(i)]=fromTables?QVector4D(rest[k],rest[k+1],rest[k+2],1):flatPoint(i,day_.length);
        cloth_.vel[size_t(i)]=fromTables?QVector4D(restVelocity[k],restVelocity[k+1],restVelocity[k+2],0):QVector4D();
    }
    cloth_.prev=cloth_.pos;
    if(!fromTables) {
        // The warm-up is a pure function of the day preset and the flat grid, so the state the constructor's worker
        // computed is usable when LINE1.mnu's day preset is bit-identical to the compiled fallback. Any other
        // dev_flash (or XMB_RIBBON_SETTLE_SYNC=1) discards it and settles synchronously, exactly as before.
        const bool matches=sameClothPreset(day_,Preset{});
        if(matches && settleFuture_.valid()) {
            QElapsedTimer join;join.start();
            Settle prefetched=settleFuture_.get();
            const double joinMs=join.nsecsElapsed()/1e6;
            cloth_=prefetched.cloth;
            qInfo().noquote()<<"ribbon: cloth warm-up"<<QString::number(prefetched.micros/1000.,'f',1)
                <<"ms on a prefetch thread started by the constructor (joined in initializeGL after"<<QString::number(joinMs,'f',1)
                <<"ms); LINE1 day preset matches the compiled fallback, state reused";
        } else {
            if(settleFuture_.valid())settleFuture_.wait();
            QElapsedTimer settle;settle.start();
            for(int i=0;i<14400;++i)cloth_.step(day_); // the x extent crumples first, then recovers (cloth-sim.ts)
            qInfo().noquote()<<"ribbon: cloth warm-up"<<QString::number(settle.nsecsElapsed()/1e6,'f',1)<<"ms synchronous"
                <<(matches?"; prefetch unavailable":"; LINE1 day preset differs from the compiled fallback");
        }
    }
    // _Stripes (FUN_0004bad0, captured): row k of 4, x in [-.5,.5]: min(1, pow(1.01 - 4x^2, 20 (k/3)^3) * (k/6 + .5)).
    constexpr int stripeWidth=512; // fit: the plugin's width is a runtime global; 512 resolves the profile
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
    fieldValid_=false;fieldPrefetched_=false;fieldFence_=nullptr;fieldPbo_=0;lodBiasValid_=false;linesValid_=false; // GL objects were just (re)created: nothing cached applies to them
    ready_=true;
    qInfo()<<"ribbon: ffd_shader1"<<ffdCg_.instructionCount<<"+ lines1"<<linesVertexCg_.instructionCount<<"/"<<linesFragmentCg_.instructionCount
           <<"instructions, FresLUT"<<lut.size()<<(fromTables?"cloth from plugin preset 1":"cloth from a settled flat grid (no --vsh-modules)");
    return true;
}

// FUN_0004a220 / FUN_00047b1c: ffd_shader1 over the node grid at _Time = 10 x the cloth's wave clock, read back,
// scaled by FFD SCALE1 plus FFD OFFSET, and padded to 11x7x7 (index clamp(x-1,0,7), clamp(y-1,0,3), clamp(z-1,0,3)).
void Ribbon::drawField(QOpenGLFunctions_3_3_Core& gl,float time) {
    QVector<QVector4D> fc=ffdCg_.constants;
    for(int slot:ffdCg_.parameters.value("_Time").constantSlots)if(slot<fc.size())fc[slot]=QVector4D(1,1,1,1)*time;
    field_->bind();gl.glViewport(0,0,128,1);gl.glDisable(GL_BLEND);
    ffd_.bind();uniformcache::setVec4Array(ffd_,"fc",fc);uniformcache::set(ffd_,"tex0",0);uvw_->bind(0);
    gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);
}

void Ribbon::deformLattice(QOpenGLFunctions_3_3_Core& gl,const Preset& p) {
    const float time=cloth_.target*10;
    if(!fieldValid_ || time!=fieldTime_) {
        bool have=false;
        if(fieldPrefetched_ && fieldPrefetchTime_==time && fieldFence_) {
            // The field for exactly this clock value was drawn and queued for read-back a step ago.
            gl.glClientWaitSync(fieldFence_,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000ull);
            gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,fieldPbo_);
            gl.glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,GLsizeiptr(fieldPixels_.size()*sizeof(float)),fieldPixels_.data());
            gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
            have=true;
        }
        if(fieldFence_){gl.glDeleteSync(fieldFence_);fieldFence_=nullptr;}
        fieldPrefetched_=false;
        if(!have) { // first use, a pinned clock, or a preset whose time step moved the prediction: the synchronous read
            drawField(gl,time);
            gl.glReadPixels(0,0,128,1,GL_RGBA,GL_FLOAT,fieldPixels_.data());
            uvw_->release(0);ffd_.release();
        }
        fieldTime_=time;fieldValid_=true;
        // Start the next step's field now: a cloth step adds TIMESTEP x 1e-4 to the clock (Cloth::step) and wraps it past 10.
        float next=cloth_.target+p.timestep*1e-4f;
        if(next>10)next=0;
        const float nextTime=next*10;
        static const bool prefetchEnabled=qEnvironmentVariable("XMB_RIBBON_NO_PREFETCH")!=QStringLiteral("1");
        if(nextTime!=time && prefetchEnabled) {
            if(!fieldPbo_){gl.glGenBuffers(1,&fieldPbo_);}
            gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,fieldPbo_);
            gl.glBufferData(GL_PIXEL_PACK_BUFFER,GLsizeiptr(fieldPixels_.size()*sizeof(float)),nullptr,GL_STREAM_READ);
            drawField(gl,nextTime);
            gl.glReadPixels(0,0,128,1,GL_RGBA,GL_FLOAT,nullptr);   // into the bound pixel buffer: queued, not waited for
            gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
            uvw_->release(0);ffd_.release();
            fieldFence_=gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            fieldPrefetchTime_=nextTime;fieldPrefetched_=true;
        }
    }
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
    clipScaleX_=sx;clipScaleY_=sy;
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
    // The 16 weight products of a sample depend only on (r % 8, c % 8), so they are formed once, with the very same float
    // products the per-sample form used; the accumulation below is the same ordered mul/add per lane (SSE, no FMA), so the
    // vertices are bit-identical to the QVector4D loop it replaces (D12, checked with XMB_RIBBON_CLIP_DUMP).
    struct PatchWeights { float position[16],du[16],dv[16]; };
    static_assert(sizeof(QVector4D)==4*sizeof(float));
    std::array<std::array<PatchWeights,8>,8> weights;
    for(int rm=0;rm<8;++rm)for(int cm=0;cm<8;++cm) {
        const auto &bv=value[size_t(rm)],&dvw=slope[size_t(rm)],&bu=value[size_t(cm)],&duw=slope[size_t(cm)];
        PatchWeights& w=weights[size_t(rm)][size_t(cm)];
        for(int a=0;a<4;++a)for(int b=0;b<4;++b) {
            w.position[a*4+b]=bv[size_t(a)]*bu[size_t(b)];w.du[a*4+b]=bv[size_t(a)]*duw[size_t(b)];w.dv[a*4+b]=dvw[size_t(a)]*bu[size_t(b)];
        }
    }
    // AVX: the eight columns of a patch row are eight lanes. Each lane runs the same ordered multiply/add chain as the
    // scalar form (no FMA, no reassociation), so the result is bit-identical; it is four times fewer instructions.
    static const bool avx=[]{
#ifdef Q_OS_WIN
        return IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)!=0 && qEnvironmentVariable("XMB_RIBBON_NO_AVX")!=QStringLiteral("1");
#else
        return false;
#endif
    }();
    if(avx) {
        alignas(32) static thread_local float lanePosition[8][16][8],laneDu[8][16][8],laneDv[8][16][8]; // [r % 8][a*4+b][c % 8]
        for(int rm=0;rm<8;++rm)for(int cm=0;cm<8;++cm)for(int ab=0;ab<16;++ab) {
            const PatchWeights& w=weights[size_t(rm)][size_t(cm)];
            lanePosition[rm][ab][cm]=w.position[ab];laneDu[rm][ab][cm]=w.du[ab];laneDv[rm][ab][cm]=w.dv[ab];
        }
        for(int r=0;r<grid_;++r) {
            const int rm=r%8,patchRow=segment[size_t(r)];
            for(int patchColumn=0;patchColumn<grid_/8;++patchColumn) {
                const QVector4D* patch=&clipLattice_[size_t(patchRow*side_+patchColumn)];
                __m256 position[4],du[4],dv[4];
                for(int k=0;k<4;++k)position[k]=du[k]=dv[k]=_mm256_setzero_ps();
                for(int a=0;a<4;++a)for(int b=0;b<4;++b) {
                    const int ab=a*4+b;
                    const float* q=reinterpret_cast<const float*>(patch+a*side_+b);
                    const __m256 wp=_mm256_load_ps(lanePosition[rm][ab]),wu=_mm256_load_ps(laneDu[rm][ab]),wv=_mm256_load_ps(laneDv[rm][ab]);
                    for(int k=0;k<4;++k) {
                        const __m256 component=_mm256_set1_ps(q[k]);
                        position[k]=_mm256_add_ps(position[k],_mm256_mul_ps(component,wp));
                        du[k]=_mm256_add_ps(du[k],_mm256_mul_ps(component,wu));
                        dv[k]=_mm256_add_ps(dv[k],_mm256_mul_ps(component,wv));
                    }
                }
                alignas(32) float outPosition[4][8],outDu[4][8],outDv[4][8];
                for(int k=0;k<4;++k){_mm256_store_ps(outPosition[k],position[k]);_mm256_store_ps(outDu[k],du[k]);_mm256_store_ps(outDv[k],dv[k]);}
                for(int lane=0;lane<8;++lane) {
                    const QVector3D normal=QVector3D::crossProduct(QVector3D(outDv[0][lane],outDv[1][lane],outDv[2][lane]),QVector3D(outDu[0][lane],outDu[1][lane],outDu[2][lane]));
                    float* v=&vertices_[(size_t(r)*grid_+size_t(patchColumn*8+lane))*12];
                    v[0]=outPosition[0][lane];v[1]=outPosition[1][lane];v[2]=outPosition[2][lane];v[3]=outPosition[3][lane];
                    v[8]=normal.x();v[9]=normal.y();v[10]=normal.z();v[11]=0;
                }
            }
        }
        _mm256_zeroupper();
        return;
    }
    for(int r=0;r<grid_;++r)for(int c=0;c<grid_;++c) {
        const PatchWeights& w=weights[size_t(r%8)][size_t(c%8)];
        const QVector4D* patch=&clipLattice_[size_t(segment[size_t(r)]*side_+segment[size_t(c)])];
        __m128 position=_mm_setzero_ps(),du=_mm_setzero_ps(),dv=_mm_setzero_ps();
        for(int a=0;a<4;++a)for(int b=0;b<4;++b) {
            const __m128 q=_mm_loadu_ps(reinterpret_cast<const float*>(patch+a*side_+b));
            position=_mm_add_ps(position,_mm_mul_ps(q,_mm_set1_ps(w.position[a*4+b])));
            du=_mm_add_ps(du,_mm_mul_ps(q,_mm_set1_ps(w.du[a*4+b])));
            dv=_mm_add_ps(dv,_mm_mul_ps(q,_mm_set1_ps(w.dv[a*4+b])));
        }
        alignas(16) float dvOut[4],duOut[4];
        _mm_store_ps(dvOut,dv);_mm_store_ps(duOut,du);
        const QVector3D normal=QVector3D::crossProduct(QVector3D(dvOut[0],dvOut[1],dvOut[2]),QVector3D(duOut[0],duOut[1],duOut[2]));
        float* v=&vertices_[(size_t(r)*grid_+c)*12];
        _mm_storeu_ps(v,position);
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
    w.toColdboot2=float(1-std::pow(.99,std::min(t,stageTwo)*60)); // captured: measured boot filter
    const double n=std::max(0.,t-stageTwo)*60;
    constexpr double knee=112,a=1.4e-5,kneeValue=a*knee*knee,kneeSlope=2*a*knee,span=168-knee; // captured: RPCS3 boot replay
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

void Ribbon::sheetMotion(std::vector<QVector3D>& world,std::vector<QVector3D>& velocity,int& side) const {
    side=0;world.clear();velocity.clear();
    if(!ready_ || lastSeconds_<0)return;
    const Preset& p=current_;
    // Cloth::swell of a cloth state at the drawn blend.
    const auto swellAt=[&](const Cloth& cloth,float acc){
        std::array<QVector4D,points_> drawn{};
        for(int i=0;i<points_;++i) {
            const QVector4D l=cloth.prev[size_t(i)]*(1-acc)+cloth.pos[size_t(i)]*acc;
            const float d=l.x()-p.ffdParam,s=std::clamp(d/5,0.f,1.f);
            const float scale=1.3f-(d<0?1.f:std::cos(d*pi/2))*(1-s)*(1-s)*(1+2*s);
            drawn[size_t(i)]=QVector4D(l.x(),l.y()*scale,l.z()*scale,1);
        }
        return drawn;
    };
    // splineSheet's FFD + camera + bicubic sheet, kept in camera space (the projection is linear in it, so the sheet of
    // the camera-space lattice is the sheet splineSheet projects), then moved into the world.
    QMatrix4x4 rotation;rotation.rotate(p.angle,p.axis);
    const QVector3D eye=p.pos+QVector3D(0,0,-2),toWorld(0,0,2);
    std::array<int,grid_> segment{};std::array<std::array<float,4>,grid_> value{};
    for(int k=0;k<grid_;++k){segment[size_t(k)]=k/8;value[size_t(k)]=basis(float(k%8)*16/127.f);}
    const auto sheet=[&](const std::array<QVector4D,points_>& drawn,std::vector<QVector3D>& out){
        std::array<QVector3D,points_> view{};
        for(int i=0;i<points_;++i) {
            const QVector3D n=(drawn[size_t(i)].toVector3D()-p.offset)/p.scale1;
            int cell[3];std::array<float,4> w[3];
            for(int a=0;a<3;++a){const float s=std::clamp(n[a],0.f,.999f)*(a==0?8.f:4.f);cell[a]=int(s);w[a]=basis(s-float(cell[a]));}
            QVector3D sum;
            for(int a=0;a<4;++a)for(int b=0;b<4;++b)for(int c=0;c<4;++c)
                sum+=deform_[size_t((cell[0]+a)+11*(cell[1]+b)+77*(cell[2]+c))].toVector3D()*(w[0][size_t(a)]*w[1][size_t(b)]*w[2][size_t(c)]);
            view[size_t(i)]=rotation.map((n+sum)*p.scale2)+eye;
        }
        out.assign(size_t(grid_)*grid_,QVector3D());
        for(int r=0;r<grid_;++r)for(int c=0;c<grid_;++c) {
            QVector3D position;
            const auto &bv=value[size_t(r)],&bu=value[size_t(c)];
            for(int a=0;a<4;++a)for(int b=0;b<4;++b)
                position+=view[size_t((segment[size_t(r)]+a)*side_+segment[size_t(c)]+b)]*(bv[size_t(a)]*bu[size_t(b)]);
            out[size_t(r)*grid_+c]=position+toWorld;
        }
    };
    // The console's emitter reads the drawn sheet: FUN_00030b80 samples a cell's clip-space vertex through the sheet
    // matrix at layer+0x50 (FUN_0002d640), which maps it back to camera space + (0, 0, 2): x / sx, y / sy, -w, plus
    // (0, 0, 2), the same world as here. Its velocity is that point minus the same cell sampled one tick earlier, over
    // DELTA TIME (0x30d8c). The earlier stand-in, the move of a once-stepped copy of the cloth, remains only for a frame
    // without a tick-old history (a pinned first frame, or after a stall), where the rest table has not stepped yet.
    if(clipScaleX_>0 && clipScaleY_>0 && vertices_.size()>=size_t(grid_)*grid_*12) {
        world.resize(size_t(grid_)*grid_);
        for(size_t i=0;i<world.size();++i){const float* v=&vertices_[i*12];world[i]=QVector3D(v[0]/clipScaleX_,v[1]/clipScaleY_,-v[3]+2.f);}
    } else sheet(swellAt(cloth_,cloth_.acc),world);
    velocity.resize(world.size());
    // The console differences across exactly one 60 Hz tick; at a higher frame rate a one-frame difference catches a
    // fraction of a tick or a whole tick's step, so the reference is the newest kept frame at least 1/60 s old.
    if(sheetHistory_.empty() || sheetHistory_.back().first!=lastSeconds_)sheetHistory_.emplace_back(lastSeconds_,world);
    while(sheetHistory_.size()>2 && lastSeconds_-sheetHistory_[1].first>=1./60)sheetHistory_.pop_front();
    const auto& reference=sheetHistory_.front();
    const double elapsed=lastSeconds_-reference.first;
    if(elapsed>=1./60-1e-6 && elapsed<=.25 && reference.second.size()==world.size()) {
        for(size_t i=0;i<world.size();++i)velocity[i]=(world[i]-reference.second[i])/float(elapsed);
    } else {
        if(elapsed>.25){sheetHistory_.clear();sheetHistory_.emplace_back(lastSeconds_,world);} // a stall: restart
        Cloth next=cloth_;
        next.step(p);
        std::vector<QVector3D> now,ahead;
        sheet(swellAt(cloth_,cloth_.acc),now);
        sheet(swellAt(next,cloth_.acc),ahead);
        for(size_t i=0;i<world.size();++i)velocity[i]=(ahead[i]-now[i])*60.f; // one cloth step is 1/60 s
    }
    side=grid_;
}

std::vector<QVector3D> Ribbon::sheetSample(int stride,int& side) const {
    side=0;
    if(!ready_ || stride<1 || lastSeconds_<0)return {};
    side=(grid_-1)/stride+1;
    std::vector<QVector3D> out;out.reserve(size_t(side)*side);
    for(int r=0;r<grid_;r+=stride)for(int c=0;c<grid_;c+=stride) {
        const float* v=&vertices_[(size_t(r)*grid_+c)*12];
        out.emplace_back(v[3]>0?v[0]/v[3]:0.f,v[3]>0?v[1]/v[3]:0.f,v[3]);
    }
    return out;
}

void Ribbon::render(QOpenGLFunctions_3_3_Core& gl,double seconds,double dayFraction,int pixelWidth,int pixelHeight,GLuint targetFbo) {
    if(!ready_ || pixelWidth<=0 || pixelHeight<=0)return;
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_SCISSOR_TEST);gl.glDisable(GL_CULL_FACE);
    // 1. The cloth (FUN_0004f814): 60 fixed steps per second, the drawn lattice interpolated between the last two.
    const double dt=lastSeconds_<0?1.0/60:std::clamp(seconds-lastSeconds_,0.0,.25);
    const bool firstFrame=lastSeconds_<0;
    lastSeconds_=seconds;
    advancePresets(dt,dayFraction);
    const Preset p=current_;
    // Measurement: XMB_RIBBON_ADVANCE=1 starts the cloth `seconds` into its animation, so a pinned --render-clock
    // shows the sheet at that time since boot instead of the boot-instant rest table (audit/PIPELINE.md "Ribbon
    // over time"). Off by default: the rest table is the state the console's vertex buffer matches.
    if(firstFrame && qEnvironmentVariableIsSet("XMB_RIBBON_ADVANCE"))for(int i=0,n=int(std::min(seconds,3600.)*60);i<n;++i)cloth_.step(p);
    cloth_.acc+=float(dt*60);
    // XMB_RIBBON_CLOTH_LOG=<file>: per cloth step, the step number, the noise counter and the first six points, for
    // comparison with the console's cloth (custom_render_plugin FUN_0004bed0, counter at +0x2d4; audit/PARTICLES.md).
    static FILE* clothLog=[]{const QByteArray path=qgetenv("XMB_RIBBON_CLOTH_LOG");return path.isEmpty()?nullptr:std::fopen(path.constData(),"w");}();
    static long long clothSteps=0;
    {idleprofile::Scope profCloth("ribbon cloth");
    while(cloth_.acc>1) {
        if(clothLog) {
            std::fprintf(clothLog,"%lld %d",clothSteps,cloth_.seed);
            for(int i=0;i<6;++i)std::fprintf(clothLog," %.7g %.7g %.7g",cloth_.pos[size_t(i)].x(),cloth_.pos[size_t(i)].y(),cloth_.pos[size_t(i)].z());
            std::fputc('\n',clothLog);
        }
        cloth_.step(p);cloth_.acc-=1;++clothSteps;
    }
    cloth_.swell(p);
    }
    // 2. The FFD lattice from ffd_shader1, 3. spline.elf's sheet.
    {idleprofile::Scope prof("ribbon deformLattice");deformLattice(gl,p);}
    const float aspect=float(pixelWidth)/float(pixelHeight);
    {idleprofile::Scope prof("ribbon splineSheet");splineSheet(p,aspect);}
    // XMB_RIBBON_CLIP_DUMP=<prefix>: the 128 x 128 clip-space sheet (x y z w per vertex, raw floats) on frames
    // XMB_RIBBON_CLIP_FROM..+XMB_RIBBON_CLIP_COUNT, for comparison with the console sheet the particle emitter reads
    // (custom_render_plugin layer+0x40, audit/PARTICLES.md).
    {
        static const QString clipDump=qEnvironmentVariable("XMB_RIBBON_CLIP_DUMP");
        static const int from=qEnvironmentVariableIntValue("XMB_RIBBON_CLIP_FROM"),count=std::max(1,qEnvironmentVariableIntValue("XMB_RIBBON_CLIP_COUNT"));
        static const int every=std::max(1,qEnvironmentVariableIntValue("XMB_RIBBON_CLIP_EVERY")); // frames between dumps
        static int frame=0;
        if(!clipDump.isEmpty() && frame>=from && (frame-from)/every<count && (frame-from)%every==0) {
            QFile file(QString("%1_%2_%3us.bin").arg(clipDump).arg(frame).arg(qint64(seconds*1e6)));
            if(file.open(QIODevice::WriteOnly))for(int i=0;i<grid_*grid_;++i)file.write(reinterpret_cast<const char*>(&vertices_[size_t(i)*12]),4*sizeof(float));
        }
        ++frame;
    }
    {idleprofile::Scope prof("ribbon crest");
    // Crest of the centre line (row 64), for the procedural background and measurement.
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
    }
    // 4. lines1 into a float sum, additively (the firmware sums RGBA8 through _Encode).
    if(!sum_ || sum_->size()!=QSize(pixelWidth,pixelHeight)) {
        QOpenGLFramebufferObjectFormat format;format.setInternalTextureFormat(GL_RGBA16F);
        sum_=std::make_unique<QOpenGLFramebufferObject>(pixelWidth,pixelHeight,format);
    }
    idleprofile::Scope profGl("ribbon gl draw");
    sum_->bind();gl.glViewport(0,0,pixelWidth,pixelHeight);gl.glClearColor(0,0,0,0);gl.glClear(GL_COLOR_BUFFER_BIT);
    gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
    const std::array<float,5> linesKey={p.fresnel,p.brightness,p.mipmapBias,p.spacing,thinness};
    if(!linesValid_ || linesKey_!=linesKey) {
        linesKey_=linesKey;linesValid_=true;
        std::vector<QVector4D>& vc=linesVc_;
        vc.assign(468,QVector4D());
        for(const auto& param:linesVertexCg_.parameters)if(param.resourceIndex>=0 && param.resourceIndex<468 && !param.defaults.isEmpty())
            vc[size_t(param.resourceIndex)]=QVector4D(param.defaults.value(0),param.defaults.value(1),param.defaults.value(2),param.defaults.value(3));
        const auto setVc=[&](const char* name,float value){const auto it=linesVertexCg_.parameters.constFind(name);if(it!=linesVertexCg_.parameters.cend() && it->resourceIndex>=0 && it->resourceIndex<468)vc[size_t(it->resourceIndex)]=QVector4D(value,value,value,value);};
        setVc("_Fresnel",p.fresnel);setVc("_Brightness",p.brightness);setVc("_MipmapBias",p.mipmapBias);
        linesFc_=linesFragmentCg_.constants;
        const auto setFc=[&](const char* name,float value){for(int slot:linesFragmentCg_.parameters.value(name).constantSlots)if(slot<linesFc_.size())linesFc_[slot]=QVector4D(value,value,value,value);};
        setFc("_Spacing",p.spacing);setFc("_Thinness",thinness);
    }
    lines_.bind();
    uniformcache::setVec4Array(lines_,"vc",linesVc_);uniformcache::setVec4Array(lines_,"fc",linesFc_);
    uniformcache::set(lines_,"tex0",0);uniformcache::set(lines_,"tex1",1);uniformcache::set(lines_,"tex2",2);
    if(!lodBiasValid_ || lodBias_!=p.mipmapBias){stripes_->setLevelofDetailBias(p.mipmapBias);lodBias_=p.mipmapBias;lodBiasValid_=true;}
    stripes_->bind(0);fresLut_->bind(1);encode_->bind(2);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(vertices_.size()*sizeof(float)),vertices_.data());
    gl.glDrawElements(GL_TRIANGLES,indexCount_,GL_UNSIGNED_INT,nullptr);
    gl.glBindVertexArray(0);
    encode_->release(2);fresLut_->release(1);stripes_->release(0);lines_.release();
    // 5. Composite onto the target (LinesController: background + lines x gradient).
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,pixelWidth,pixelHeight);
    composite_.bind();uniformcache::set(composite_,"sum",0);uniformcache::set(composite_,"colour3",colour3_);uniformcache::set(composite_,"colour4",colour4_);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,sum_->texture());
    gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);gl.glBindVertexArray(0);
    gl.glBindTexture(GL_TEXTURE_2D,0);composite_.release();
    gl.glDisable(GL_BLEND);
}
}
