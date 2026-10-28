#include "particles.h"
#include "assets.h"
#include "qrc.h"
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <QDebug>

namespace xmb {
namespace {
// --- PARTICLES.mnu (base preset; the four ordinary XMB themes day/night/yoake/higure carry the same
// physics block byte for byte - audit/PARTICLES.md 3.4) -------------------------------------------
constexpr double kDeltaTime=0.0088883;           // DELTA TIME, the fixed simulation step (s)
constexpr float kEmitPerFrame=16.6539f;          // EMIT PER FRAME, candidates per 60 Hz frame
constexpr float kEmitProb=0.479115f;             // EMIT PROB, per-candidate accept probability
constexpr float kEmitVelMin=0.15064f;            // EMIT VEL MIN, speed floor
constexpr float kEmitVelMul=0.19f;               // EMIT VEL MUL
constexpr float kEmitVelVar=0.282567f;            // EMIT VEL VAR
constexpr float kEmitConeDegrees=51.8695f;       // EMIT CONE ANGLE, half-angle
constexpr float kEmitNegProb=0.173899f;          // EMIT NEG PROB, 17.4% of spawns mirrored
constexpr float kAgingSpeed=0.00285223f;         // AGING SPEED, age increment per step
constexpr float kAgingVariance=0.493003f;        // AGING VARIANCE, per-particle lifetime spread
constexpr float kFriction=0.030551f;             // FRICTION, per-step velocity drag
constexpr float kGravity=-6.8e-5f;               // GRAVITY, y acceleration per step
constexpr float kBrownian=0.225311f;             // BROWNIAN SCALE
constexpr float kSpinTimeScale=2.74f;            // SPIN TIME SCALE, radians per second
constexpr float kLifeFull=0.94f;                 // life envelope: full until this age ...
constexpr float kLifeFade=0.06f;                 // ... then linear to 0 over this much life
constexpr float kLifeIn=0.02f;                   // ... and a matching clamp at the start of life
constexpr float kReferenceFrameRate=60.f;        // EMIT PER FRAME is a 60 Hz budget (audit 3.3/3.5)
// Fitted semantics for the two open constants (audit 6-A3: FRICTION is applied per step, which the
// 0.287..0.394 per-step speeds and the ~3 s lifetime make self-consistent; BROWNIAN's per-step
// velocity kick amplitude is fitted so the dust wanders without dominating the drift).
constexpr float kBrownianStep=0.05f;
constexpr int kMaxCatchUpSteps=4096;             // per render call: 4096 * 0.0088883 s ~= 36 s of sim,
                                                 // so --render-clock 30 first frame reaches t=30 in one
                                                 // call while a long stall cannot freeze the app for
                                                 // more than that.
constexpr unsigned int kSeed=0x5053u;            // fixed generator seed: determinism, never rand()
// --- look / geometry ----------------------------------------------------------------------------
// World units are the firmware's particle units. A 22-unit-tall viewport puts PARTICLES.mnu's
// SIZE MIDDLE (0.0536) at a ~2.6 px dot at 1080p, which matches the RPCS3 reference captures, and
// the 0.287..0.394 units/step speeds then cross ~half the screen over a mean lifetime.
constexpr float kScreenHeight=22.f;
constexpr float kHalfHeight=kScreenHeight*.5f;
// Emission origin is the one unknown the audit leaves open (6-A2: no emitter asset exists and the
// program's state block is physics, not an emitter mesh). Fitted to the reference frames: dust is
// emitted across the full width, denser below the centre line (where the XMB's dust reads), in a
// depth band between the near and far bounds.
constexpr float kEmitYCentre=-3.f;   // world units, screen centre is 0 and +11 is the top edge
constexpr float kEmitYSpread=5.5f;   // triangular spread around that centre
constexpr float kEmitZNear=-3.f;     // depth band (world z is negative, away from the camera)
constexpr float kEmitZFar=-13.5f;
constexpr float kEmitXMargin=1.25f;  // emit past the viewport edges so the edges never thin out
// --- the one untraced colour term (audit 6-B2) ---------------------------------------------------
// PARTICLES.mnu has no key for either value and neither was captured: `_Color` is a float3 set
// somewhere in the plugin and `_Gamma` is the float written through FUN_0005c200. They multiply the
// lit term in the same slot of both fragment programs (`rgb = _Color * lit * _Gamma`), so this is a
// single joint gain, and it is the only fitted photometric number in this file.
// The fit is the previous port's 1.05 (the value that put the dust at the reference layer's
// brightness) divided by the mean of the now-applied fresnel term: the shading normal is a uniform
// random quaternion, so E[|N.V|] = 0.5 and E[T] = 1 - 1/(1 + FRESNEL/2) = 1 - 1/2.333 = 0.571.
// 1.05 / 0.571 = 1.84 keeps the same average exposure with the evidenced alpha chain in place.
constexpr float kColorGammaGain=1.84f;
constexpr float kIridescentExp=1.f;              // PARTICLES.mnu IRIDESCENT EXP
// --- pass 1: particles_quads ---------------------------------------------------------------------
const char* vertexSource=R"(#version 330 core
// particles_quads vertex contract (audit/PARTICLES.md 2.2): float4 position with the per-vertex life
// alpha in w, float2 uv0 billboard corner, float4 per-particle orientation quaternion.
layout(location=0) in vec4 aPos;
layout(location=8) in vec2 aUv;
layout(location=11) in vec4 aRot;
uniform vec2 uHalfExtent;   // world units spanned by half the viewport (screen height = 22 units)
uniform float uGlobalAlpha; // PARTICLES.mnu GLOBAL ALPHA (_Transparency.y)
out vec2 vUv;
out float vLife;
out float vLambert;
out float vSpecular;
out vec2 vIridescent;
out float vDefocus;
vec3 rotateQuat(vec4 q,vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
void main(){
    vec3 lightDir=normalize(vec3(0.55,0.42,0.72));           // PARTICLES.mnu SPOT POS: upper-right key light
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));   // the camera looks down the view axis, +z
    vec4 q=aRot/length(aRot);
    // Depth of field: sharp at the focus plane, larger toward SIZE FAR. The defocus term only reaches
    // part of the way to SIZE FAR (the fully defocused bound), and the 1.15 size gain is fitted to the
    // RPCS3 reference captures - the fpo's SIZE->corner mapping is the piece of DoF packing the audit
    // could not recover (6-B3). Most dust ends up a 3-5 px speck, with the far particles as the big
    // soft glints.
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-6.12)/7.4,0.0,1.0);        // NEAR/FAR FOCUS bands (6-B3 fit)
    float size=mix(0.0536233,0.874062,defocus*defocus*0.12)*1.15; // SIZE MIDDLE -> FAR
    // Billboarding: the firmware's _FrontFacingQuaternion keeps the quads camera-facing, so the corner
    // spins in the screen plane and the quad never turns edge-on into a streak. The full quaternion
    // still drives the shading normal below, which is what makes the dust glint as it turns.
    float yaw=atan(2.0*(q.x*q.y+q.w*q.z),1.0-2.0*(q.y*q.y+q.z*q.z));
    float cs=cos(yaw),sn=sin(yaw);
    vec2 corner=aUv*2.0-1.0;
    vec2 spun=vec2(corner.x*cs-corner.y*sn,corner.x*sn+corner.y*cs);
    vec3 world=aPos.xyz+vec3(spun*size*0.5,0.0);
    gl_Position=vec4(world.x/uHalfExtent.x,world.y/uHalfExtent.y,0.0,1.0);
    vec3 normal=rotateQuat(q,vec3(0.0,0.0,1.0));
    vLambert=max(dot(normal,lightDir),0.0);
    vSpecular=pow(max(dot(normal,halfVector),0.0),35.2904);  // PARTICLES.mnu SPECULAR POWER
    // The iridescent lookup coordinate: the fpo samples at normal_v * 0.5 + 0.5, and normal_v is the
    // view-facing orientation normal, so the sample is one point of the ramp per particle - the dust
    // shifts hue as it spins instead of showing the ramp across the quad.
    vIridescent=normal.xy*0.5+0.5;
    // Alpha chain (vpo lines 186-203, applied per vertex as the program does): fresnel edge term
    // T = 1 - fresnel^FRESNEL, then GLOBAL ALPHA, the SPU's per-vertex life alpha, and the depth
    // stand-in for the life-bounds box / NEAR-FAR DARKNESS / DoF fades (audit 6-B3 leaves their
    // packing open; this is the same fitted curve the layer was tuned with).
    float fresnel=1.0-abs(dot(normal,vec3(0.0,0.0,1.0)));
    float T=clamp(1.0-pow(fresnel,1.33319),0.0,1.0);         // PARTICLES.mnu FRESNEL
    float depthFade=clamp(1.0-(depth-3.0)/10.5,0.10,1.0);
    vLife=aPos.w*uGlobalAlpha*T*depthFade*depthFade;
    vUv=aUv;
    vDefocus=defocus;
})";
const char* fragmentSource=R"(#version 330 core
in vec2 vUv;
in float vLife;
in float vLambert;
in float vSpecular;
in vec2 vIridescent;
in float vDefocus;
out vec4 oColor;
uniform sampler2D uIridescent;  // the firmware particles/proc_iridescent.tga
uniform float uIridescentExp;   // PARTICLES.mnu IRIDESCENT EXP
uniform float uHasIridescent;   // 0 only if no texture could be created at all
uniform float uColorGain;       // _Color * _Gamma (audit 6-B2, the one fitted gain)
void main(){
    vec2 c=vUv*2.0-1.0;
    float r2=dot(c,c);
    if(r2>1.0)discard;
    // Soft disc from the fpo's radial terms (fc[15].z 0.05 core, fc[22] 3 rim): a solid little disc with a
    // soft edge for the in-focus dust, and a much softer falloff for the defocused glints.
    float disc=clamp((1.0-r2)*mix(2.2,0.6,vDefocus),0.0,1.0);
    // PARTICLES.mnu photometry: LAMBERT COEFF 8, SPECULAR COEFF 74.74, SPECULAR POWER 35.2904,
    // EXPOSURE 0.0390458, COLOR CONTROL 1; exposure is the fpo's 1 - e^(-EXPOSURE * light).
    float lit=uColorGain*(8.0*vLambert+74.74*vSpecular);
    float exposure=1.0-exp(-0.0390458*lit);
    // Iridescence: the fpo samples _IridescentTex at normal_v and raises each channel to
    // _IridescentExp (t + 1, built as (t+1)^exp). The texture is a saturated pink/violet/amber ramp,
    // so it is used as hue only: divided by its own luma it passes brightness through untouched, and
    // the small weights keep the dust neutral-white - the RPCS3 reference frame's dust delta is
    // (29,29,29), i.e. colourless, with the ramp visible only where the specular lobe peaks.
    vec3 tint=vec3(1.0);
    if(uHasIridescent>0.5) {
        vec3 iridescent=pow(texture(uIridescent,vIridescent).rgb+vec3(1.0),vec3(uIridescentExp));
        float luma=max(dot(iridescent,vec3(0.2126,0.7152,0.0722)),1e-4);
        vec3 chroma=clamp(iridescent/luma,0.55,1.45);
        tint=mix(vec3(1.0),chroma,0.10+0.50*vSpecular);
    }
    float intensity=disc*exposure*vLife*(1.0-0.85*vDefocus);
    oColor=vec4(tint*intensity,1.0);
})";
// --- pass 2: particles_second --------------------------------------------------------------------
// The same particles again, GLARE SCALE times larger, carrying the fpo's radial halo into the HDR
// glare source (alpha-audit PARTICLES.md 2.6.6): intensity = GLARE * e^(-P2 * r^P1). This pass has
// its own program because it has a different shape (no disc, no iridescent tint, no alpha test) and
// its own target - the folded radial term that used to ride in pass 1's fragment program is gone.
const char* glareVertexSource=R"(#version 330 core
layout(location=0) in vec4 aPos;
layout(location=8) in vec2 aUv;
layout(location=11) in vec4 aRot;
uniform vec2 uHalfExtent;
uniform float uGlobalAlpha;
out vec2 vUv;
out float vLife;
out float vLambert;
out float vSpecular;
vec3 rotateQuat(vec4 q,vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
void main(){
    vec3 lightDir=normalize(vec3(0.55,0.42,0.72));
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));
    vec4 q=aRot/length(aRot);
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-6.12)/7.4,0.0,1.0);
    // GLARE SCALE (5.44386) is what makes the glints read as bloom seeds rather than dots; the
    // second vpo multiplies the corner extent by _Glare.y (slot 459) - glsl/particles_second.vpo.glsl
    // line 172-175.
    float size=mix(0.0536233,0.874062,defocus*defocus*0.12)*1.15*5.44386;
    float yaw=atan(2.0*(q.x*q.y+q.w*q.z),1.0-2.0*(q.y*q.y+q.z*q.z));
    float cs=cos(yaw),sn=sin(yaw);
    vec2 corner=aUv*2.0-1.0;
    vec2 spun=vec2(corner.x*cs-corner.y*sn,corner.x*sn+corner.y*cs);
    vec3 world=aPos.xyz+vec3(spun*size*0.5,0.0);
    gl_Position=vec4(world.x/uHalfExtent.x,world.y/uHalfExtent.y,0.0,1.0);
    vec3 normal=rotateQuat(q,vec3(0.0,0.0,1.0));
    vLambert=max(dot(normal,lightDir),0.0);
    vSpecular=pow(max(dot(normal,halfVector),0.0),35.2904);
    // The fpo carries the alpha chain in o[10].w only, which an additive blend into the glare source
    // cannot use. Riding the same chain in the intensity is what stops the glare from blooming dust
    // that pass 1 does not draw (fresnel edge, depth fade, GLOBAL ALPHA 0 during the cold boot).
    float fresnel=1.0-abs(dot(normal,vec3(0.0,0.0,1.0)));
    float T=clamp(1.0-pow(fresnel,1.33319),0.0,1.0);
    float depthFade=clamp(1.0-(depth-3.0)/10.5,0.10,1.0);
    vLife=aPos.w*uGlobalAlpha*T*depthFade*depthFade;
    vUv=aUv;
})";
const char* glareFragmentSource=R"(#version 330 core
in vec2 vUv;
in float vLife;
in float vLambert;
in float vSpecular;
out vec4 oColor;
uniform float uColorGain;   // the same _Color * _Gamma gain as pass 1
void main(){
    // Centred uv radius, as the second fpo builds it: r = |2 * (uv0 - 0.5)|.
    float r=length(vUv*2.0-1.0);
    // PARTICLES.mnu GLARE P1 0.99 / P2 4.44 / GLARE 0.159705:
    // halo = GLARE * exp(-P2 * r^P1), the fpo's radial falloff.
    float halo=exp(-4.44*pow(max(r,1e-4),0.99));
    float lit=uColorGain*(8.0*vLambert+74.74*vSpecular);
    float exposure=1.0-exp(-0.0390458*lit);
    float intensity=0.159705*exposure*halo*vLife;
    oColor=vec4(vec3(intensity),1.0);
})";
// The four quad corners, as the uv0 billboard corner the vpo centres: (0,0),(1,0),(0,1),(1,1).
constexpr float kCornerU[4]={0.f,1.f,0.f,1.f};
constexpr float kCornerV[4]={0.f,0.f,1.f,1.f};
}

float Particles::random() {
    // The firmware's RNG wrapper (particles.elf 0x33d8): Park-Miller, seed = seed * 16807 mod (2^31-1).
    seed_=unsigned((static_cast<unsigned long long>(seed_)*16807ull)%2147483647ull);
    return float(seed_)*(1.f/2147483647.f);
}

unsigned Particles::stateChecksum() const {
    // FNV-1a over the live pool, for XMB_PARTICLE_LOG determinism checks.
    unsigned hash=2166136261u;
    const auto mix=[&hash](const void* data,size_t bytes){
        const unsigned char* p=static_cast<const unsigned char*>(data);
        for(size_t i=0;i<bytes;++i){hash^=p[i];hash*=16777619u;}
    };
    for(int i=0;i<live_;++i) {
        const Particle& p=pool_[size_t(i)];
        mix(&p.px,8*sizeof(float)); // px,py,pz,vx,vy,vz,age,ageRate
        mix(&p.qx,7*sizeof(float)); // quaternion + spin axis
    }
    return hash;
}

void Particles::resetSimulation() {
    live_=0;
    step_=0;
    spawnAccumulator_=0;
    seed_=kSeed;
    std::fill(pool_.begin(),pool_.end(),Particle{});
}

void Particles::spawnParticle() {
    Particle& p=pool_[size_t(live_++)];
    // Emission volume (see kEmit* above).
    p.px=(2.f*random()-1.f)*kHalfHeight*aspect_*kEmitXMargin;
    p.py=kEmitYCentre+(random()+random()-1.f)*kEmitYSpread;
    p.pz=kEmitZNear+random()*(kEmitZFar-kEmitZNear);
    // Velocity: speed EMIT VEL MIN + MUL*(1 +/- VAR), in a cone around +x in the screen plane, z
    // flattened by EMIT VEL ZSCALE 0, with EMIT NEG PROB of the spawns mirrored.
    const float speed=kEmitVelMin+kEmitVelMul*(1.f+kEmitVelVar*(2.f*random()-1.f));
    const float angle=(2.f*random()-1.f)*kEmitConeDegrees*(3.14159265f/180.f);
    const float mirror=random()<kEmitNegProb?-1.f:1.f;
    p.vx=mirror*speed*std::cos(angle);
    p.vy=mirror*speed*std::sin(angle);
    p.vz=0.f;
    p.age=0.f;
    p.ageRate=kAgingSpeed*(1.f+kAgingVariance*(2.f*random()-1.f));
    // Orientation: a random unit quaternion; the same axis carries the spin (SPIN TIME SCALE rad/s).
    const float z=2.f*random()-1.f, phi=6.2831853f*random(), radial=std::sqrt(std::max(0.f,1.f-z*z));
    p.ax=radial*std::cos(phi);
    p.ay=radial*std::sin(phi);
    p.az=z;
    const float halfAngle=3.14159265f*random();
    const float s=std::sin(halfAngle);
    p.qx=p.ax*s;
    p.qy=p.ay*s;
    p.qz=p.az*s;
    p.qw=std::cos(halfAngle);
}

void Particles::step() {
    // Emission: EMIT PER FRAME x EMIT PROB is a 60 Hz frame budget, converted to the DELTA TIME step
    // (the SPU integrates at 112.5 Hz while the PPU queues spawn commands per rendered frame). The
    // steady state then lands just under the cap: 1529 live of 1536 measured, against the audit's
    // central estimate of 1492, the difference being the 1/(1 +/- AGING VARIANCE) lifetime averaging
    // above its centre.
    spawnAccumulator_+=kEmitPerFrame*(float(kDeltaTime)*kReferenceFrameRate);
    while(spawnAccumulator_>=1.f) {
        spawnAccumulator_-=1.f;
        const bool accepted=random()<kEmitProb; // always drawn, so the sequence depends only on time
        if(accepted && live_<kCapacity)spawnParticle();
    }
    const float drag=1.f-kFriction;
    const float kick=kBrownian*kBrownianStep;
    // Quaternion spin: q = spinStep * q, SPIN TIME SCALE radians per second.
    const float halfSpin=.5f*kSpinTimeScale*float(kDeltaTime);
    const float spinC=std::cos(halfSpin),spinS=std::sin(halfSpin);
    for(int i=live_-1;i>=0;--i) {
        Particle& p=pool_[size_t(i)];
        // Brownian velocity kick, one RNG draw per axis (audit 3.3), then gravity and per-step friction.
        p.vx+=kick*(2.f*random()-1.f);
        p.vy+=kick*(2.f*random()-1.f)+kGravity;
        p.vz+=kick*(2.f*random()-1.f);
        p.vx*=drag;
        p.vy*=drag;
        p.vz*=drag;
        p.px+=p.vx;
        p.py+=p.vy;
        p.pz+=p.vz;
        p.age+=p.ageRate;
        if(p.age>=1.f) { // life over: swap the last live particle in, it was already stepped this frame
            pool_[size_t(i)]=pool_[size_t(--live_)];
            continue;
        }
        const float bx=p.ax*spinS,by=p.ay*spinS,bz=p.az*spinS,bw=spinC;
        const float qw=p.qw,qx=p.qx,qy=p.qy,qz=p.qz;
        p.qw=bw*qw-bx*qx-by*qy-bz*qz;
        p.qx=bw*qx+bx*qw+by*qz-bz*qy;
        p.qy=bw*qy-bx*qz+by*qw+bz*qx;
        p.qz=bw*qz+bx*qy-by*qx+bz*qw;
        const float length=std::sqrt(p.qx*p.qx+p.qy*p.qy+p.qz*p.qz+p.qw*p.qw);
        if(length>0.f) {
            const float inverse=1.f/length;
            p.qx*=inverse;p.qy*=inverse;p.qz*=inverse;p.qw*=inverse;
        }
    }
    ++step_;
}

bool Particles::initPrograms() {
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexSource) ||
       !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentSource) || !program_.link()) {
        qWarning().noquote()<<"particles: particles_quads link failed:"<<program_.log();
        return false;
    }
    if(!glareProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex,glareVertexSource) ||
       !glareProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment,glareFragmentSource) || !glareProgram_.link()) {
        qWarning().noquote()<<"particles: particles_second link failed:"<<glareProgram_.log();
        return false;
    }
    // One allocation each, once: the pool and the per-frame vertex staging area.
    pool_.resize(kCapacity);
    vertices_.resize(size_t(kCapacity)*4); // 4 vertices per particle, as the firmware draws
    resetSimulation();
    indexOffset_=GLintptr(vertices_.size()*sizeof(Vertex));
    return true;
}

bool Particles::initResources(QOpenGLFunctions_3_3_Core& gl) {
    // One buffer object: the 48-byte vertex records, then the static index list. The VAO keeps the
    // buffer bound as its element array, so a frame is one glBufferSubData plus one draw call per
    // pass. Both programs use the same contract, so both passes share this VAO.
    const GLsizeiptr vertexBytes=GLsizeiptr(vertices_.size()*sizeof(Vertex));
    const GLsizeiptr indexBytes=GLsizeiptr(size_t(kCapacity)*6*sizeof(unsigned int));
    std::vector<unsigned int> indices(size_t(kCapacity)*6);
    for(int i=0;i<kCapacity;++i) {
        unsigned int* const d=indices.data()+size_t(i)*6;
        const unsigned int base=unsigned(i)*4u;
        d[0]=base;d[1]=base+1;d[2]=base+2; // corners (0,0),(1,0),(0,1)
        d[3]=base+2;d[4]=base+1;d[5]=base+3; // (0,1),(1,0),(1,1)
    }
    gl.glGenVertexArrays(1,&vao_);
    gl.glGenBuffers(1,&vbo_);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferData(GL_ARRAY_BUFFER,vertexBytes+indexBytes,nullptr,GL_DYNAMIC_DRAW);
    gl.glBufferSubData(GL_ARRAY_BUFFER,indexOffset_,indexBytes,indices.data());
    // particles_quads attributes 0 / 8 / 11 (POSITION / TEXCOORD0 / TEXCOORD3), 48-byte stride.
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,GLsizei(sizeof(Vertex)),reinterpret_cast<const void*>(0));
    gl.glEnableVertexAttribArray(8);
    gl.glVertexAttribPointer(8,2,GL_FLOAT,GL_FALSE,GLsizei(sizeof(Vertex)),reinterpret_cast<const void*>(sizeof(float)*4));
    gl.glEnableVertexAttribArray(11);
    gl.glVertexAttribPointer(11,4,GL_FLOAT,GL_FALSE,GLsizei(sizeof(Vertex)),reinterpret_cast<const void*>(sizeof(float)*8));
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,vbo_);
    gl.glBindVertexArray(0);
    gl.glBindBuffer(GL_ARRAY_BUFFER,0);
    ready_=true;
    return true;
}

bool Particles::createTexture(const QImage& image) {
    if(image.isNull())return false;
    const QImage rgba=image.convertToFormat(QImage::Format_RGBA8888);
    if(rgba.isNull())return false;
    iridescent_=std::make_unique<QOpenGLTexture>(rgba,QOpenGLTexture::GenerateMipMaps);
    iridescent_->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    iridescent_->setMagnificationFilter(QOpenGLTexture::Linear);
    iridescent_->setWrapMode(QOpenGLTexture::ClampToEdge);
    return true;
}

bool Particles::loadIridescent(const QString& flashRoot) {
    if(flashRoot.isEmpty()) {
        qWarning()<<"particles: no --dev-flash on the command line; the iridescent LUT is unavailable";
        return false;
    }
    // The plugin loads particles/proc_iridescent.tga out of lines.qrc (custom_render_all.c:42258).
    // QrcArchive + Assets::decodeTga is the same pair ribbon.cpp uses for its FresLUT, so the bytes
    // are the firmware's; nothing is extracted or re-encoded.
    try {
        QrcArchive archive(QDir(flashRoot).filePath("vsh/resource/qgl/lines.qrc"));
        const QByteArray tga=archive.file("particles/proc_iridescent.tga");
        const QImage image=Assets::decodeTga(tga);
        if(image.isNull()) {
            qWarning()<<"particles: proc_iridescent.tga did not decode as a TGA";
            return false;
        }
        if(!createTexture(image)) {
            qWarning()<<"particles: proc_iridescent.tga could not be uploaded";
            return false;
        }
        qInfo().noquote()<<"particles: iridescent LUT from particles/proc_iridescent.tga,"
                        <<image.width()<<"x"<<image.height()<<"RGBA";
        return true;
    } catch(const std::exception& e) {
        qWarning().noquote()<<"particles: iridescent LUT unavailable:"<<e.what();
        return false;
    }
}

bool Particles::init(QOpenGLFunctions_3_3_Core& gl) {
    if(!initPrograms())return false;
    // View owns the Assets service and does not pass it to this layer, so the firmware flash root is
    // taken from the same --dev-flash argument the process was started with (main.cpp requires it).
    // init(gl, assets) is the preferred entry point; with it this lookup disappears.
    QString flashRoot=qEnvironmentVariable("XMB_DEV_FLASH");
    if(flashRoot.isEmpty()) {
        const QStringList arguments=QCoreApplication::arguments();
        for(int i=0;i<arguments.size();++i) {
            if(arguments[i]=="--dev-flash" && i+1<arguments.size()) { flashRoot=arguments[i+1];break; }
            if(arguments[i].startsWith("--dev-flash=")) { flashRoot=arguments[i].section('=',1);break; }
        }
    }
    iridescentFromFirmware_=loadIridescent(flashRoot);
    if(!iridescentFromFirmware_) {
        // Neutral 1x1: (1,1,1) + 1 raised to IRIDESCENT EXP is a flat luma, so the chroma term is
        // exactly white and the dust keeps its photometry without a fabricated rainbow ramp.
        QImage white(1,1,QImage::Format_RGBA8888);
        white.fill(QColor(255,255,255,255));
        if(!createTexture(white))qWarning()<<"particles: no iridescent texture at all; tint stays white";
    }
    if(!initResources(gl))return false;
    qInfo()<<"particles: firmware pool"<<kCapacity<<"records, DELTA TIME"<<kDeltaTime<<"s,"
           <<"EMIT"<<kEmitPerFrame<<"x"<<kEmitProb<<"(60 Hz), cone"<<kEmitConeDegrees<<"deg"
           <<"("<<kEmitNegProb<<" mirrored), AGING"<<kAgingSpeed<<"+/-"<<kAgingVariance
           <<", FRICTION"<<kFriction<<", GRAVITY"<<kGravity<<", BROWNIAN"<<kBrownian
           <<", SPIN"<<kSpinTimeScale<<"rad/s, seed"<<kSeed;
    qInfo()<<"particles: two passes, particles_quads (HDR scene) and particles_second (glare source,"
           <<"GLARE 0.159705 P1 0.99 P2 4.44 SCALE 5.44386), _Color*_Gamma gain"<<kColorGammaGain;
    return true;
}

bool Particles::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    if(!initPrograms())return false;
    // The preferred path: read the texture through the view's own asset service.
    try {
        const QImage image=Assets::decodeTga(assets.qglFile("lines","particles/proc_iridescent.tga"));
        iridescentFromFirmware_=createTexture(image);
        if(!iridescentFromFirmware_)qWarning()<<"particles: proc_iridescent.tga unavailable through Assets";
    } catch(const std::exception& e) {
        qWarning().noquote()<<"particles: proc_iridescent.tga unavailable through Assets:"<<e.what();
    }
    if(!iridescentFromFirmware_) {
        QImage white(1,1,QImage::Format_RGBA8888);
        white.fill(QColor(255,255,255,255));
        createTexture(white);
    }
    if(!initResources(gl))return false;
    qInfo()<<"particles: firmware pool"<<kCapacity<<"records, iridescent from Assets:"
           <<iridescentFromFirmware_<<"two passes, _Color*_Gamma gain"<<kColorGammaGain;
    return true;
}

void Particles::sync(QOpenGLFunctions_3_3_Core& gl,double seconds) {
    // Fixed-step sim: the state is a pure function of the elapsed seconds and the fixed seed, which is
    // what makes --render-clock captures reproducible. A long stall catches up a bounded number of
    // steps per call instead of spawning a burst or blocking the frame.
    const long long target=seconds>0.0?static_cast<long long>(std::floor(seconds/kDeltaTime)):0;
    if(target<step_)resetSimulation(); // the clock went backwards: replay from the seed
    for(int n=0;step_<target && n<kMaxCatchUpSteps;++n)step();
    // Per-vertex life alpha: the SPU envelope, full until 0.94 then linear to 0 at 1.0, with the
    // kernel's 0.02 start clamp.
    const auto lifeEnvelope=[](float age){
        const float in=std::min(age/kLifeIn,1.f);
        const float out=1.f-std::max(age-kLifeFull,0.f)/kLifeFade;
        return std::clamp(in,0.f,1.f)*std::clamp(out,0.f,1.f);
    };
    Vertex* const out=vertices_.data();
    for(int i=0;i<live_;++i) {
        const Particle& p=pool_[size_t(i)];
        const float life=lifeEnvelope(p.age);
        for(int corner=0;corner<4;++corner) {
            Vertex& v=out[size_t(i)*4+corner];
            v.x=p.px;v.y=p.py;v.z=p.pz;v.life=life;
            v.u=kCornerU[corner];v.v=kCornerV[corner];v.pad0=0.f;v.pad1=0.f;
            v.qx=p.qx;v.qy=p.qy;v.qz=p.qz;v.qw=p.qw;
        }
    }
    // Refresh the VBO only when something actually changed: the second pass of the same frame then
    // draws the buffer the first pass uploaded.
    if(step_!=uploadedStep_ || live_!=uploadedLive_ || aspect_!=uploadedAspect_) {
        if(live_>0) {
            gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
            gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(size_t(live_)*4*sizeof(Vertex)),vertices_.data());
            gl.glBindBuffer(GL_ARRAY_BUFFER,0);
        }
        uploadedStep_=step_;
        uploadedLive_=live_;
        uploadedAspect_=aspect_;
    }
}

void Particles::logState(const char* pass,int width,int height) const {
    static const bool logState=qEnvironmentVariableIsSet("XMB_PARTICLE_LOG");
    if(!logState)return;
    int nonFinite=0;
    for(int i=0;i<live_;++i) {
        const Particle& p=pool_[size_t(i)];
        if(!std::isfinite(p.px)||!std::isfinite(p.py)||!std::isfinite(p.pz)||
           !std::isfinite(p.vx)||!std::isfinite(p.vy)||!std::isfinite(p.vz)||
           !std::isfinite(p.age)||!std::isfinite(p.qx)||!std::isfinite(p.qy)||
           !std::isfinite(p.qz)||!std::isfinite(p.qw))++nonFinite;
    }
    qInfo().noquote()<<"particles:"<<pass<<"step"<<step_<<"live"<<live_
        <<"nonfinite"<<nonFinite<<"checksum"<<QString::number(stateChecksum(),16)
        <<"target"<<width<<"x"<<height<<"lut"<<(iridescentFromFirmware_?"tga":"fallback");
}

void Particles::drawQuads(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight) {
    if(live_<=0)return;
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glEnable(GL_BLEND);
    gl.glBlendFunc(GL_ONE,GL_ONE); // additive, as the firmware's HDR-scene pass reads
    gl.glBindVertexArray(vao_);
    if(iridescent_)iridescent_->bind(0);
    program_.bind();
    program_.setUniformValue("uHalfExtent",float(kHalfHeight*aspect_),kHalfHeight);
    program_.setUniformValue("uGlobalAlpha",globalAlpha_);
    program_.setUniformValue("uColorGain",kColorGammaGain);
    program_.setUniformValue("uIridescentExp",kIridescentExp);
    program_.setUniformValue("uHasIridescent",iridescent_?1.f:0.f);
    program_.setUniformValue("uIridescent",0);
    gl.glDrawElements(GL_TRIANGLES,GLsizei(size_t(live_)*6),GL_UNSIGNED_INT,reinterpret_cast<const void*>(indexOffset_));
    program_.release();
    if(iridescent_)iridescent_->release(0);
    gl.glBindVertexArray(0);
    gl.glDisable(GL_BLEND);
}

void Particles::drawGlare(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight) {
    if(live_<=0)return;
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glEnable(GL_BLEND);
    gl.glBlendFunc(GL_ONE,GL_ONE); // additive into the glare source, then the mip blur blooms it
    gl.glBindVertexArray(vao_);
    glareProgram_.bind();
    const float aspect=pixelWidth>0&&pixelHeight>0?float(pixelWidth)/float(pixelHeight):aspect_;
    glareProgram_.setUniformValue("uHalfExtent",float(kHalfHeight*aspect),kHalfHeight);
    glareProgram_.setUniformValue("uGlobalAlpha",globalAlpha_);
    glareProgram_.setUniformValue("uColorGain",kColorGammaGain);
    gl.glDrawElements(GL_TRIANGLES,GLsizei(size_t(live_)*6),GL_UNSIGNED_INT,reinterpret_cast<const void*>(indexOffset_));
    glareProgram_.release();
    gl.glBindVertexArray(0);
    gl.glDisable(GL_BLEND);
}

void Particles::render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight) {
    if(!ready_ || pixelWidth<=0 || pixelHeight<=0)return;
    // The unmodified view.cpp calls render() for both firmware passes. The glare-source call is the
    // one that arrives straight after a scene draw, for the same elapsed seconds, with a different
    // draw framebuffer (Hdr binds its glare source around the extraGlare hook). Explicit renderGlare()
    // calls skip this test.
    GLint fbo=0;
    gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    if(previousWasScene_ && seconds==previousSeconds_ && fbo!=previousFbo_) {
        previousWasScene_=false;
        logState("second",pixelWidth,pixelHeight);
        drawGlare(gl,pixelWidth,pixelHeight);
        return;
    }
    previousWasScene_=true;
    previousSeconds_=seconds;
    previousFbo_=fbo;
    aspect_=float(pixelWidth)/float(pixelHeight);
    sync(gl,seconds);
    logState("quads",pixelWidth,pixelHeight);
    drawQuads(gl,pixelWidth,pixelHeight);
}

void Particles::renderGlare(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight) {
    if(!ready_ || pixelWidth<=0 || pixelHeight<=0)return;
    previousWasScene_=false;
    sync(gl,seconds); // a no-op when pass 1 already stepped to this time
    logState("second",pixelWidth,pixelHeight);
    drawGlare(gl,pixelWidth,pixelHeight);
}
}
