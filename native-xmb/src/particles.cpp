#include "particles.h"
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
constexpr float kEmitVelVar=0.282567f;           // EMIT VEL VAR
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
constexpr float kHalfHeight=kScreenHeight*.5f;// Emission origin is the one unknown the audit leaves open (6-A2: no emitter asset exists and the
// program's state block is physics, not an emitter mesh). Fitted to the reference frames: dust is
// emitted across the full width, denser below the centre line (where the XMB's dust reads), in a
// depth band between the near and far bounds.
constexpr float kEmitYCentre=-3.f;   // world units, screen centre is 0 and +11 is the top edge
constexpr float kEmitYSpread=5.5f;   // triangular spread around that centre
constexpr float kEmitZNear=-3.f;     // depth band (world z is negative, away from the camera)
constexpr float kEmitZFar=-13.5f;
constexpr float kEmitXMargin=1.25f;  // emit past the viewport edges so the edges never thin out
// particles_second's glare is folded into the same quad; see the fragment program.
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
out float vIridescence;
out float vDefocus;
vec3 rotateQuat(vec4 q,vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
void main(){
    vec3 lightDir=normalize(vec3(0.55,0.42,0.72));           // PARTICLES.mnu SPOT POS: upper-right key light
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));   // the camera looks down the view axis, +z
    vec3 sizeBands=vec3(0.0772033,0.0536233,0.874062);       // PARTICLES.mnu SIZE NEAR/MIDDLE/FAR
    vec2 focus=vec2(6.12,7.4);                                // focus depth, defocus falloff (audit 6-B3 fit)
    vec4 q=aRot/length(aRot);
    // Depth of field: sharp at the focus plane, larger toward SIZE FAR. The defocus term only reaches part
    // of the way to SIZE FAR (the fully defocused bound), and the 1.15 size gain is fitted to the RPCS3
    // reference captures - the fpo's SIZE->corner mapping is the piece of DoF packing the audit could not
    // recover (6-B3). Most dust ends up a 3-5 px speck, with the far particles as the big soft glints.
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-focus.x)/focus.y,0.0,1.0);
    float size=mix(sizeBands.y,sizeBands.z,defocus*defocus*0.12)*1.15;
    // Billboarding: the firmware's _FrontFacingQuaternion keeps the quads camera-facing, so the corner
    // spins in the screen plane and the quad never turns edge-on into a streak. The full quaternion still
    // drives the shading normal below, which is what makes the dust glint as it turns.
    float yaw=atan(2.0*(q.x*q.y+q.w*q.z),1.0-2.0*(q.y*q.y+q.z*q.z));
    float cs=cos(yaw),sn=sin(yaw);
    vec2 corner=aUv*2.0-1.0;
    vec2 spun=vec2(corner.x*cs-corner.y*sn,corner.x*sn+corner.y*cs);
    vec3 world=aPos.xyz+vec3(spun*size*0.5,0.0);
    gl_Position=vec4(world.x/uHalfExtent.x,world.y/uHalfExtent.y,0.0,1.0);
    vec3 normal=rotateQuat(q,vec3(0.0,0.0,1.0));
    vLambert=max(dot(normal,lightDir),0.0);
    vSpecular=pow(max(dot(normal,halfVector),0.0),35.2904);    // PARTICLES.mnu SPECULAR POWER
    vIridescence=0.5+0.5*dot(normal,vec3(0.31,0.47,0.83));
    // The untraced part of the alpha chain (audit 6-B3: fresnel/life bounds/darkness packing): dust
    // deep in the scene reads fainter, which is what keeps the reference captures to a few dozen
    // visible specks instead of a uniform field.
    float depthFade=clamp(1.0-(depth-3.0)/10.5,0.10,1.0);
    vLife=aPos.w*uGlobalAlpha*depthFade*depthFade;
    vUv=aUv;
    vDefocus=defocus;
})";
const char* fragmentSource=R"(#version 330 core
in vec2 vUv;
in float vLife;
in float vLambert;
in float vSpecular;
in float vIridescence;
in float vDefocus;
out vec4 oColor;
void main(){
    vec2 c=vUv*2.0-1.0;
    float r2=dot(c,c);
    if(r2>1.0)discard;
    // Soft disc from the fpo's radial terms (fc[15].z 0.05 core, fc[22] 3 rim): a solid little disc with a
    // soft edge for the in-focus dust, and a much softer falloff for the defocused glints.
    float disc=clamp((1.0-r2)*mix(2.2,0.6,vDefocus),0.0,1.0);
    // PARTICLES.mnu photometry: LAMBERT COEFF 8, SPECULAR COEFF 74.74, SPECULAR POWER 35.2904,
    // EXPOSURE 0.0390458, COLOR CONTROL 1. _Color and _Gamma were never traced (audit 6-B2), so the lit
    // term carries a fitted gain that puts the dust at the reference layer's brightness.
    float lit=1.05*(8.0*vLambert+74.74*vSpecular);
    float exposure=1.0-exp(-0.0390458*lit);
    // Iridescent tint (proc_iridescent.tga is not reachable from this file): warm-white dust that turns
    // coloured only where the quad's normal sweeps through the highlight, as the reference captures show.
    vec3 iridescent=0.5+0.5*cos(6.2831853*(vIridescence+vec3(0.0,0.33,0.67)));
    vec3 tint=mix(vec3(1.0,0.96,0.88),iridescent,0.18+0.55*vSpecular);
    // particles_second's radial glare (GLARE 0.159705, GLARE P1 0.99, P2 4.44), folded into this quad:
    // the real second pass writes into the HDR glare source, which lives outside this file.
    float glare=0.045*exp(-4.44*pow(sqrt(r2),0.99));
    float intensity=(disc+glare)*exposure*vLife*(1.0-0.85*vDefocus);
    oColor=vec4(tint*intensity,1.0);
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

bool Particles::init(QOpenGLFunctions_3_3_Core& gl) {
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexSource) ||
       !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentSource) || !program_.link()) {
        qWarning().noquote()<<"particles: link failed:"<<program_.log();
        return false;
    }
    // One allocation each, once: the pool and the per-frame vertex staging area.
    pool_.resize(kCapacity);
    vertices_.resize(size_t(kCapacity)*4); // 4 vertices per particle, as the firmware draws
    resetSimulation();
    // One buffer object: the 48-byte vertex records, then the static index list. The VAO keeps the
    // buffer bound as its element array, so a frame is one glBufferSubData plus one draw call.
    const GLsizeiptr vertexBytes=GLsizeiptr(vertices_.size()*sizeof(Vertex));
    const GLsizeiptr indexBytes=GLsizeiptr(size_t(kCapacity)*6*sizeof(unsigned int));
    std::vector<unsigned int> indices(size_t(kCapacity)*6);
    for(int i=0;i<kCapacity;++i) {
        unsigned int* const d=indices.data()+size_t(i)*6;
        const unsigned int base=unsigned(i)*4u;
        d[0]=base;d[1]=base+1;d[2]=base+2; // corners (0,0),(1,0),(0,1)
        d[3]=base+2;d[4]=base+1;d[5]=base+3; // (0,1),(1,0),(1,1)
    }
    indexOffset_=GLintptr(vertexBytes);
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
    qInfo()<<"particles: firmware pool"<<kCapacity<<"records, DELTA TIME"<<kDeltaTime<<"s,"
           <<"EMIT"<<kEmitPerFrame<<"x"<<kEmitProb<<"(60 Hz), cone"<<kEmitConeDegrees<<"deg"
           <<"("<<kEmitNegProb<<" mirrored), AGING"<<kAgingSpeed<<"+/-"<<kAgingVariance
           <<", FRICTION"<<kFriction<<", GRAVITY"<<kGravity<<", BROWNIAN"<<kBrownian
           <<", SPIN"<<kSpinTimeScale<<"rad/s, seed"<<kSeed;
    return true;
}

void Particles::render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight) {
    if(!ready_ || pixelWidth<=0 || pixelHeight<=0)return;
    aspect_=float(pixelWidth)/float(pixelHeight);
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
    if(static const bool logState=qEnvironmentVariableIsSet("XMB_PARTICLE_LOG");logState) {
        int nonFinite=0;
        for(int i=0;i<live_;++i) {
            const Particle& p=pool_[size_t(i)];
            if(!std::isfinite(p.px)||!std::isfinite(p.py)||!std::isfinite(p.pz)||
               !std::isfinite(p.vx)||!std::isfinite(p.vy)||!std::isfinite(p.vz)||
               !std::isfinite(p.age)||!std::isfinite(p.qx)||!std::isfinite(p.qy)||
               !std::isfinite(p.qz)||!std::isfinite(p.qw))++nonFinite;
        }
        qInfo().noquote()<<"particles: step"<<step_<<"of"<<target<<"live"<<live_
            <<"nonfinite"<<nonFinite<<"checksum"<<QString::number(stateChecksum(),16);
    }
    if(live_<=0)return;
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glEnable(GL_BLEND);
    gl.glBlendFunc(GL_ONE,GL_ONE); // additive, as the firmware's HDR-scene pass reads
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(size_t(live_)*4*sizeof(Vertex)),vertices_.data());
    program_.bind();
    program_.setUniformValue("uHalfExtent",float(kHalfHeight*aspect_),kHalfHeight);
    program_.setUniformValue("uGlobalAlpha",globalAlpha_);
    gl.glDrawElements(GL_TRIANGLES,GLsizei(size_t(live_)*6),GL_UNSIGNED_INT,reinterpret_cast<const void*>(indexOffset_));
    program_.release();
    gl.glBindVertexArray(0);
    gl.glBindBuffer(GL_ARRAY_BUFFER,0);
    gl.glDisable(GL_BLEND);
}
}
