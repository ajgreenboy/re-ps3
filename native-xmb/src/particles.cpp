#include "particles.h"
#include "assets.h"
#include "qrc.h"
#include "rco_objects.h"
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <QDebug>

namespace xmb {
// --- the scene path's shared reader (see particles.h) ---------------------------------------------
namespace {
struct SceneReader {
    Assets* assets=nullptr;
    std::unique_ptr<RcoObjects> objects;
    std::unique_ptr<Materials> materials;
    int mnuFiles=0;
    qint64 mnuNanoseconds=0;
    bool reported=false;
};
SceneReader* activeReader=nullptr;
SceneReader& reader(Assets& assets) {
    static SceneReader instance;
    if(instance.assets!=&assets) {
        instance=SceneReader{};
        instance.assets=&assets;
        instance.objects=std::make_unique<RcoObjects>(assets);
        instance.materials=std::make_unique<Materials>(assets,*instance.objects);
        activeReader=&instance;
    }
    return instance;
}
}
RcoObjects& sceneObjects(Assets& assets) { return *reader(assets).objects; }
Materials& sceneMaterials(Assets& assets) { return *reader(assets).materials; }
void sceneNoteMaterials(int files,qint64 nanoseconds) {
    if(!activeReader)return;
    activeReader->mnuFiles+=files;
    activeReader->mnuNanoseconds+=nanoseconds;
}
void logSceneReader(const char* where) {
    if(!activeReader || activeReader->reported)return;
    activeReader->reported=true;
    const RcoObjects::Cost cost=activeReader->objects->cost();
    qInfo().noquote()<<"scene reader:"<<where<<":"
        <<cost.containers<<"RCO containers,"<<cost.bytesRead<<"B,"
        <<QString::number(double(cost.nanoseconds)/1e6,'f',2)+" ms;"
        <<activeReader->mnuFiles<<"MNU files,"
        <<QString::number(double(activeReader->mnuNanoseconds)/1e6,'f',2)+" ms"
        <<"(lazy: no scene layer reads an RCO object tree; the scene path's values live in the QGL MNU files)";
}

namespace {
// --- constants that stay in this file ------------------------------------------------------------
// Every one is either a fit (no RCO/MNU key carries it) or a capture (a value the firmware's own
// bytes gave, but not through an MNU key). The firmware values this layer used to duplicate now come
// from PARTICLES.mnu through Materials; the fallbacks are in Particles::Physics/Look.
constexpr float kLifeFull=0.94f;           // captured: kernel life envelope full until this age
constexpr float kLifeFade=0.06f;           // captured: ... then linear to 0 over this much life
constexpr float kLifeIn=0.02f;             // captured: ... and the matching clamp at the start of life
constexpr float kReferenceFrameRate=60.f;  // captured: EMIT PER FRAME is a 60 Hz budget (audit 3.3/3.5)
// Fitted semantics for the two open constants (audit 6-A3: FRICTION is applied per step, which the
// 0.287..0.394 per-step speeds and the ~3 s lifetime make self-consistent; BROWNIAN's per-step
// velocity kick amplitude is fitted so the dust wanders without dominating the drift).
constexpr float kBrownianStep=0.05f;       // fit: per-step Brownian kick amplitude
constexpr int kMaxCatchUpSteps=4096;       // derived: 4096 * 0.0088883 s ~= 36 s of sim, so
                                           // --render-clock 30 reaches t=30 in one call while a long
                                           // stall cannot freeze the app for more than that.
constexpr unsigned int kSeed=0x5053u;      // fit: fixed seed so --render-clock captures reproduce;
                                           // the firmware's own seed is not traced (Park-Miller itself is)
// --- look / geometry ----------------------------------------------------------------------------
// World units are the firmware's particle units. A 22-unit-tall viewport puts PARTICLES.mnu's
// SIZE MIDDLE (0.0536 in the base file) at a ~2.6 px dot at 1080p, which matches the RPCS3 reference
// captures, and the 0.287..0.394 units/step speeds then cross ~half the screen over a mean lifetime.
constexpr float kScreenHeight=22.f;        // fit: world-units-per-viewport-height fitted to the captures
constexpr float kHalfHeight=kScreenHeight*.5f;
// Emission origin is the one unknown the audit leaves open (6-A2: no emitter asset exists and the
// program's state block is physics, not an emitter mesh). Fitted to the reference frames: dust is
// emitted across the full width, denser below the centre line (where the XMB's dust reads), in a
// depth band between the near and far bounds.
constexpr float kEmitYCentre=-3.f;   // fit: emission volume, world units (+11 is the top edge)
constexpr float kEmitYSpread=5.5f;   // fit: triangular spread around that centre
constexpr float kEmitZNear=-3.f;     // fit: depth band (world z is negative, away from the camera)
constexpr float kEmitZFar=-13.5f;    // fit: depth band far edge
constexpr float kEmitXMargin=1.25f;  // fit: emit past the viewport edges so the edges never thin out
// --- the one untraced colour term (audit 6-B2) ---------------------------------------------------
// PARTICLES.mnu has no key for either value and neither was captured: `_Color` is a float3 set
// somewhere in the plugin and `_Gamma` is the float written through FUN_0005c200. They multiply the
// lit term in the same slot of both fragment programs (`rgb = _Color * lit * _Gamma`), so this is a
// single joint gain, and it is the only fitted photometric number in this file.
// The fit is the previous port's 1.05 (the value that put the dust at the reference layer's
// brightness) divided by the mean of the now-applied fresnel term: the shading normal is a uniform
// random quaternion, so E[|N.V|] = 0.5 and E[T] = 1 - 1/(1 + FRESNEL/2) = 1 - 1/2.333 = 0.571.
// 1.05 / 0.571 = 1.84 keeps the same average exposure with the evidenced alpha chain in place.
constexpr float kColorGammaGain=1.84f;     // fit: _Color * _Gamma (audit 6-B2, still untraced)
// --- pass 1: particles_quads ---------------------------------------------------------------------
const char* vertexSource=R"(#version 330 core
// particles_quads vertex contract (audit/PARTICLES.md 2.2): float4 position with the per-vertex life
// alpha in w, float2 uv0 billboard corner, float4 per-particle orientation quaternion.
layout(location=0) in vec4 aPos;
layout(location=8) in vec2 aUv;
layout(location=11) in vec4 aRot;
uniform vec2 uHalfExtent;   // world units spanned by half the viewport (screen height = 22 units)
uniform float uGlobalAlpha; // PARTICLES.mnu GLOBAL ALPHA (_Transparency.y)
uniform vec3 uSpotDir;      // normalize(PARTICLES.mnu SPOT POS)
uniform float uSpecularPower;   // PARTICLES.mnu SPECULAR POWER
uniform float uSizeMiddle;      // PARTICLES.mnu SIZE MIDDLE (theme preset)
uniform float uSizeFar;         // PARTICLES.mnu SIZE FAR
uniform float uNearFocus;       // PARTICLES.mnu NEAR FOCUS
uniform float uFarFocus;        // PARTICLES.mnu FAR FOCUS
uniform float uFresnelExp;      // PARTICLES.mnu FRESNEL
out vec2 vUv;
out float vLife;
out float vLambert;
out float vSpecular;
out vec2 vIridescent;
out float vDefocus;
vec3 rotateQuat(vec4 q,vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
void main(){
    // The firmware's spot, normalised (uSpotDir). The vpo's use of the spot is not decoded: it may
    // normalise (spot - particle) per vertex, but a fixed direction is what the port has evidence for.
    vec3 lightDir=uSpotDir;
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));   // the camera looks down the view axis, +z
    vec4 q=aRot/length(aRot);
    // Depth of field: sharp at NEAR FOCUS, fully defocused at FAR FOCUS (both PARTICLES.mnu). The
    // defocus term only reaches part of the way to SIZE FAR (the fully defocused bound), and the 1.15
    // size gain is fitted to the RPCS3 reference captures - the fpo's SIZE->corner mapping is the
    // piece of DoF packing the audit could not recover (6-B3). Most dust ends up a 3-5 px speck, with
    // the far particles as the big soft glints.
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-uNearFocus)/max(uFarFocus-uNearFocus,1e-4),0.0,1.0);
    float size=mix(uSizeMiddle,uSizeFar,defocus*defocus*0.12)*1.15; // fit: 0.12 span gain, 1.15 size gain
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
    vSpecular=pow(max(dot(normal,halfVector),0.0),uSpecularPower);
    // The iridescent lookup coordinate: the fpo samples at normal_v * 0.5 + 0.5, and normal_v is the
    // view-facing orientation normal, so the sample is one point of the ramp per particle - the dust
    // shifts hue as it spins instead of showing the ramp across the quad.
    vIridescent=normal.xy*0.5+0.5;
    // Alpha chain (vpo lines 186-203, applied per vertex as the program does): fresnel edge term
    // T = 1 - fresnel^FRESNEL, then GLOBAL ALPHA, the SPU's per-vertex life alpha, and the depth
    // stand-in for the life-bounds box / NEAR-FAR DARKNESS / DoF fades (audit 6-B3 leaves their
    // packing open; this is the same fitted curve the layer was tuned with).
    float fresnel=1.0-abs(dot(normal,vec3(0.0,0.0,1.0)));
    float T=clamp(1.0-pow(fresnel,uFresnelExp),0.0,1.0);
    float depthFade=clamp(1.0-(depth-3.0)/10.5,0.10,1.0); // fit: depth fade stand-in for 6-B3
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
uniform float uLambertCoeff;    // PARTICLES.mnu LAMBERT COEFF
uniform float uSpecularCoeff;   // PARTICLES.mnu SPECULAR COEFF
uniform float uExposure;        // PARTICLES.mnu EXPOSURE
void main(){
    vec2 c=vUv*2.0-1.0;
    float r2=dot(c,c);
    if(r2>1.0)discard;
    // Soft disc from the fpo's radial terms (fc[15].z 0.05 core, fc[22] 3 rim): a solid little disc with a
    // soft edge for the in-focus dust, and a much softer falloff for the defocused glints.
    float disc=clamp((1.0-r2)*mix(2.2,0.6,vDefocus),0.0,1.0); // fit: 2.2/0.6 disc edge falloff
    // PARTICLES.mnu photometry: LAMBERT COEFF, SPECULAR COEFF, SPECULAR POWER (vertex), EXPOSURE,
    // COLOR CONTROL 1; exposure is the fpo's 1 - e^(-EXPOSURE * light).
    float lit=uColorGain*(uLambertCoeff*vLambert+uSpecularCoeff*vSpecular);
    float exposure=1.0-exp(-uExposure*lit);
    // Iridescence: the fpo samples _IridescentTex at normal_v and raises each channel to
    // _IridescentExp (t + 1, built as (t+1)^exp). The texture is a saturated pink/violet/amber ramp,
    // so it is used as hue only: divided by its own luma it passes brightness through untouched, and
    // the small weights keep the dust neutral-white - the RPCS3 reference frame's dust delta is
    // (29,29,29), i.e. colourless, with the ramp visible only where the specular lobe peaks.
    // The 0.55..1.45 chroma clamp and the 0.10 + 0.50 * specular weight are fits (audit 6-B2).
    vec3 tint=vec3(1.0);
    if(uHasIridescent>0.5) {
        vec3 iridescent=pow(texture(uIridescent,vIridescent).rgb+vec3(1.0),vec3(uIridescentExp));
        float luma=max(dot(iridescent,vec3(0.2126,0.7152,0.0722)),1e-4);
        vec3 chroma=clamp(iridescent/luma,0.55,1.45);
        tint=mix(vec3(1.0),chroma,0.10+0.50*vSpecular);
    }
    float intensity=disc*exposure*vLife*(1.0-0.85*vDefocus); // fit: 0.85 defocus dim
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
uniform vec3 uSpotDir;
uniform float uSpecularPower;
uniform float uSizeMiddle;
uniform float uSizeFar;
uniform float uNearFocus;
uniform float uFarFocus;
uniform float uGlareScale;      // PARTICLES.mnu GLARE SCALE
uniform float uFresnelExp;
out vec2 vUv;
out float vLife;
out float vLambert;
out float vSpecular;
vec3 rotateQuat(vec4 q,vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
void main(){
    vec3 lightDir=uSpotDir;
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));
    vec4 q=aRot/length(aRot);
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-uNearFocus)/max(uFarFocus-uNearFocus,1e-4),0.0,1.0);
    // GLARE SCALE is what makes the glints read as bloom seeds rather than dots; the second vpo
    // multiplies the corner extent by _Glare.y (slot 459) - glsl/particles_second.vpo.glsl line 172-175.
    float size=mix(uSizeMiddle,uSizeFar,defocus*defocus*0.12)*1.15*uGlareScale; // fit: 0.12 span, 1.15 size
    float yaw=atan(2.0*(q.x*q.y+q.w*q.z),1.0-2.0*(q.y*q.y+q.z*q.z));
    float cs=cos(yaw),sn=sin(yaw);
    vec2 corner=aUv*2.0-1.0;
    vec2 spun=vec2(corner.x*cs-corner.y*sn,corner.x*sn+corner.y*cs);
    vec3 world=aPos.xyz+vec3(spun*size*0.5,0.0);
    gl_Position=vec4(world.x/uHalfExtent.x,world.y/uHalfExtent.y,0.0,1.0);
    vec3 normal=rotateQuat(q,vec3(0.0,0.0,1.0));
    vLambert=max(dot(normal,lightDir),0.0);
    vSpecular=pow(max(dot(normal,halfVector),0.0),uSpecularPower);
    // The fpo carries the alpha chain in o[10].w only, which an additive blend into the glare source
    // cannot use. Riding the same chain in the intensity is what stops the glare from blooming dust
    // that pass 1 does not draw (fresnel edge, depth fade, GLOBAL ALPHA 0 during the cold boot).
    float fresnel=1.0-abs(dot(normal,vec3(0.0,0.0,1.0)));
    float T=clamp(1.0-pow(fresnel,uFresnelExp),0.0,1.0);
    float depthFade=clamp(1.0-(depth-3.0)/10.5,0.10,1.0); // fit: depth fade stand-in for 6-B3
    vLife=aPos.w*uGlobalAlpha*T*depthFade*depthFade;
    vUv=aUv;
})";
const char* glareFragmentSource=R"(#version 330 core
in vec2 vUv;
in float vLife;
in float vLambert;
in float vSpecular;
out vec4 oColor;
uniform float uColorGain;       // the same _Color * _Gamma gain as pass 1
uniform float uLambertCoeff;    // PARTICLES.mnu LAMBERT COEFF
uniform float uSpecularCoeff;   // PARTICLES.mnu SPECULAR COEFF
uniform float uExposure;        // PARTICLES.mnu EXPOSURE
uniform float uGlare;           // PARTICLES.mnu GLARE
uniform float uGlareP1;         // PARTICLES.mnu GLARE P1
uniform float uGlareP2;         // PARTICLES.mnu GLARE P2
void main(){
    // Centred uv radius, as the second fpo builds it: r = |2 * (uv0 - 0.5)|.
    float r=length(vUv*2.0-1.0);
    // halo = GLARE * exp(-P2 * r^P1), the fpo's radial falloff.
    float halo=exp(-uGlareP2*pow(max(r,1e-4),uGlareP1));
    float lit=uColorGain*(uLambertCoeff*vLambert+uSpecularCoeff*vSpecular);
    float exposure=1.0-exp(-uExposure*lit);
    float intensity=uGlare*exposure*halo*vLife;
    oColor=vec4(vec3(intensity),1.0);
})";
// The four quad corners, as the uv0 billboard corner the vpo centres: (0,0),(1,0),(0,1),(1,1).
constexpr float kCornerU[4]={0.f,1.f,0.f,1.f};   // captured: particles_quads vertex contract
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

double Particles::dayFraction() const {
    // The same clock View hands to the ribbon: --background-clock overrides the local time of day,
    // otherwise the live clock. View owns that option and does not pass it to this layer, so the
    // layer re-reads the argument (the same pattern as --dev-flash on the no-Assets entry point).
    const QDateTime when=clockOverride_.isValid()?clockOverride_:QDateTime::currentDateTime();
    return double(when.time().msecsSinceStartOfDay())/86400000.0;
}

void Particles::refreshLook() {
    // The plugin's keyframe table (FUN_00011600, the same one ribbon.cpp clockPreset uses): night to
    // 01:00, to yoake by 05:00, held to 07:00, to day by 11:00, held to 13:00, to higure by 17:00,
    // held to 19:00, back to night by 23:00. Linear between neighbours.
    const struct { double at; int preset; } keys[]={{0,0},{1/24.,0},{5/24.,1},{7/24.,1},{11/24.,2},{13/24.,2},
        {17/24.,3},{19/24.,3},{23/24.,0},{1,0}};
    const double t=std::clamp(dayFraction(),0.0,1.0);
    int a=0,b=0;double f=0;
    for(size_t i=1;i<std::size(keys);++i)
        if(t<keys[i].at) {
            a=keys[i-1].preset;b=keys[i].preset;
            f=(t-keys[i-1].at)/(keys[i].at-keys[i-1].at);
            break;
        }
    const Look& A=themes_[a];
    const Look& B=themes_[b];
    const auto mix=[f](float x,float y){return x+(y-x)*float(f);};
    Look out;
    out.sizeMiddle=mix(A.sizeMiddle,B.sizeMiddle);
    out.sizeFar=mix(A.sizeFar,B.sizeFar);
    out.nearFocus=mix(A.nearFocus,B.nearFocus);
    out.farFocus=mix(A.farFocus,B.farFocus);
    out.glare=mix(A.glare,B.glare);
    out.glareScale=mix(A.glareScale,B.glareScale);
    out.glareP1=mix(A.glareP1,B.glareP1);
    out.glareP2=mix(A.glareP2,B.glareP2);
    out.specularPower=mix(A.specularPower,B.specularPower);
    out.specularCoeff=mix(A.specularCoeff,B.specularCoeff);
    out.lambertCoeff=mix(A.lambertCoeff,B.lambertCoeff);
    out.exposure=mix(A.exposure,B.exposure);
    out.fresnel=mix(A.fresnel,B.fresnel);
    out.iridescentExp=mix(A.iridescentExp,B.iridescentExp);
    look_=out;
}

void Particles::loadMaterials(Assets& assets) {
    Materials& materials=sceneMaterials(assets);
    QElapsedTimer timer;
    timer.start();
    int files=0;
    physics_=Physics{};
    lookBase_=Look{};
    const Materials::Mnu& base=materials.particles();
    ++files;
    if(base.ok) {
        physics_.deltaTime=base.number(QStringLiteral("delta time"),physics_.deltaTime);
        const auto physics=[&](const char* key,float& out){out=float(base.number(QString::fromLatin1(key),out));};
        physics("emit per frame",physics_.emitPerFrame);
        physics("emit prob",physics_.emitProb);
        physics("emit vel min",physics_.emitVelMin);
        physics("emit vel mul",physics_.emitVelMul);
        physics("emit vel var",physics_.emitVelVar);
        physics("emit cone angle",physics_.emitConeDegrees);
        physics("emit neg prob",physics_.emitNegProb);
        physics("emit vel zscale",physics_.emitVelZScale);
        physics("aging speed",physics_.agingSpeed);
        physics("aging variance",physics_.agingVariance);
        physics("friction",physics_.friction);
        physics("gravity",physics_.gravity);
        physics("brownian scale",physics_.brownian);
        physics("spin time scale",physics_.spinTimeScale);
        // EMIT VEL ZSCALE is the z extent of the emission cone; every preset this build schedules has 0.
        // A non-zero one (welcome_1/2) is out of scope, and the log says so instead of silently flattening.
        if(physics_.emitVelZScale!=0.f)
            qWarning()<<"particles: EMIT VEL ZSCALE"<<physics_.emitVelZScale<<"is not ported; the cone stays flat";
        const auto look=[&](const char* key,float& out){out=float(base.number(QString::fromLatin1(key),out));};
        look("size middle",lookBase_.sizeMiddle);
        look("size far",lookBase_.sizeFar);
        look("near focus",lookBase_.nearFocus);
        look("far focus",lookBase_.farFocus);
        look("glare",lookBase_.glare);
        look("glare scale",lookBase_.glareScale);
        look("glare p1",lookBase_.glareP1);
        look("glare p2",lookBase_.glareP2);
        look("specular power",lookBase_.specularPower);
        look("specular coeff",lookBase_.specularCoeff);
        look("lambert coeff",lookBase_.lambertCoeff);
        look("exposure",lookBase_.exposure);
        look("fresnel",lookBase_.fresnel);
        look("iridescent exp",lookBase_.iridescentExp);
        materialsLoaded_=true;
    } else {
        qWarning().noquote()<<"particles: PARTICLES.mnu unavailable ("<<base.source()<<base.error
                           <<"); using the compiled fallbacks from PARTICLES.mnu 4.93";
    }
    // The four presets the background clock can schedule. A missing override inherits the base file
    // per key (Materials returns the caller's fallback), exactly as a preset without a PARTICLES.mnu
    // inherits it in the firmware.
    const auto lookFrom=[&](const Materials::Mnu& mnu,const Look& fallback){
        Look out=fallback;
        if(!mnu.ok)return out;
        const auto get=[&](const char* key,float& out){out=float(mnu.number(QString::fromLatin1(key),out));};
        get("size middle",out.sizeMiddle);
        get("size far",out.sizeFar);
        get("near focus",out.nearFocus);
        get("far focus",out.farFocus);
        get("glare",out.glare);
        get("glare scale",out.glareScale);
        get("glare p1",out.glareP1);
        get("glare p2",out.glareP2);
        get("specular power",out.specularPower);
        get("specular coeff",out.specularCoeff);
        get("lambert coeff",out.lambertCoeff);
        get("exposure",out.exposure);
        get("fresnel",out.fresnel);
        get("iridescent exp",out.iridescentExp);
        return out;
    };
    static const char* const names[4]={"night","yoake","day","higure"};
    for(int i=0;i<4;++i) {
        const Materials::Mnu& mnu=materials.particles(QString::fromLatin1(names[i]));
        ++files;
        themes_[i]=lookFrom(mnu,lookBase_);
    }
    // SPOT POS is theme-independent (4.16, 2.63, -7.6 in the base file and every override that
    // carries it). Normalised, the shading direction; the fallbacks are the firmware's own values.
    double sx=4.16,sy=2.63,sz=-7.6; // fallback: PARTICLES.mnu spot pos x/y/z
    if(base.ok) {
        sx=base.number(QStringLiteral("spot pos x"),sx);
        sy=base.number(QStringLiteral("spot pos y"),sy);
        sz=base.number(QStringLiteral("spot pos z"),sz);
    }
    QVector3D spot{float(sx),float(sy),float(sz)};
    if(spot.lengthSquared()>1e-8f) {
        spotDirection_=spot.normalized();
    } else {
        // A zero SPOT POS cannot be normalised; keep the header's fallback (the same firmware value) and say so.
        qWarning()<<"particles: PARTICLES.mnu SPOT POS is zero; keeping normalize(4.16, 2.63, -7.6)";
    }
    // --background-clock: the theme look follows the same option View resolves for the ribbon.
    const QStringList arguments=QCoreApplication::arguments();
    for(int i=0;i<arguments.size();++i) {
        QString text;
        if(arguments[i]==QStringLiteral("--background-clock") && i+1<arguments.size())text=arguments[i+1];
        else if(arguments[i].startsWith(QStringLiteral("--background-clock=")))text=arguments[i].section('=',1);
        if(text.isEmpty())continue;
        const QDateTime when=QDateTime::fromString(text,Qt::ISODate);
        if(when.isValid())clockOverride_=when;
        break;
    }
    look_=lookBase_;
    refreshLook();
    sceneNoteMaterials(files,timer.nsecsElapsed());
    if(!materialsLoaded_)
        qWarning()<<"particles: theme looks also unavailable; the base fallbacks drive the dust";
}

void Particles::spawnParticle() {
    Particle& p=pool_[size_t(live_++)];
    // Emission volume (see kEmit* above).
    p.px=(2.f*random()-1.f)*kHalfHeight*aspect_*kEmitXMargin;
    p.py=kEmitYCentre+(random()+random()-1.f)*kEmitYSpread;
    p.pz=kEmitZNear+random()*(kEmitZFar-kEmitZNear);
    // Velocity: speed EMIT VEL MIN + MUL*(1 +/- VAR), in a cone around +x in the screen plane, z
    // flattened by EMIT VEL ZSCALE (0 in every XMB preset this build schedules), with EMIT NEG PROB
    // of the spawns mirrored. The z axis of the cone is not traced, so the read zscale only has to
    // be zero for the port to match the firmware; a non-zero one is logged in init.
    const float speed=physics_.emitVelMin+physics_.emitVelMul*(1.f+physics_.emitVelVar*(2.f*random()-1.f));
    const float angle=(2.f*random()-1.f)*physics_.emitConeDegrees*(3.14159265f/180.f);
    const float mirror=random()<physics_.emitNegProb?-1.f:1.f;
    p.vx=mirror*speed*std::cos(angle);
    p.vy=mirror*speed*std::sin(angle);
    p.vz=0.f;
    p.age=0.f;
    p.ageRate=physics_.agingSpeed*(1.f+physics_.agingVariance*(2.f*random()-1.f));
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
    spawnAccumulator_+=physics_.emitPerFrame*(float(physics_.deltaTime)*kReferenceFrameRate);
    while(spawnAccumulator_>=1.f) {
        spawnAccumulator_-=1.f;
        const bool accepted=random()<physics_.emitProb; // always drawn, so the sequence depends only on time
        if(accepted && live_<kCapacity)spawnParticle();
    }
    const float drag=1.f-physics_.friction;
    const float kick=physics_.brownian*kBrownianStep;
    // Quaternion spin: q = spinStep * q, SPIN TIME SCALE radians per second.
    const float halfSpin=.5f*physics_.spinTimeScale*float(physics_.deltaTime);
    const float spinC=std::cos(halfSpin),spinS=std::sin(halfSpin);
    for(int i=live_-1;i>=0;--i) {
        Particle& p=pool_[size_t(i)];
        // Brownian velocity kick, one RNG draw per axis (audit 3.3), then gravity and per-step friction.
        p.vx+=kick*(2.f*random()-1.f);
        p.vy+=kick*(2.f*random()-1.f)+physics_.gravity;
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
    // init(gl, assets) is the preferred entry point; with it this lookup disappears. This overload
    // has no Assets, so it cannot read the MNU files: it runs on the compiled fallbacks and says so.
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
    qWarning()<<"particles: no Assets service on this entry point; PARTICLES.mnu is not read and the"
                "compiled fallbacks drive the dust (view.cpp's init(gl,assets) path reads it)";
    qInfo()<<"particles: firmware pool"<<kCapacity<<"records, DELTA TIME"<<physics_.deltaTime<<"s,"
           <<"EMIT"<<physics_.emitPerFrame<<"x"<<physics_.emitProb<<"(60 Hz), cone"<<physics_.emitConeDegrees<<"deg"
           <<"("<<physics_.emitNegProb<<" mirrored), AGING"<<physics_.agingSpeed<<"+/-"<<physics_.agingVariance
           <<", FRICTION"<<physics_.friction<<", GRAVITY"<<physics_.gravity<<", BROWNIAN"<<physics_.brownian
           <<", SPIN"<<physics_.spinTimeScale<<"rad/s, seed"<<kSeed<<"(fallback MNU values)";
    qInfo()<<"particles: two passes, particles_quads (HDR scene) and particles_second (glare source,"
           <<"GLARE"<<look_.glare<<"P1"<<look_.glareP1<<"P2"<<look_.glareP2<<"SCALE"<<look_.glareScale
           <<"), _Color*_Gamma gain"<<kColorGammaGain;
    logSceneReader("particles");
    return true;
}

bool Particles::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    if(!initPrograms())return false;
    loadMaterials(assets);
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
    qInfo()<<"particles: firmware pool"<<kCapacity<<"records, DELTA TIME"<<physics_.deltaTime<<"s,"
           <<"EMIT"<<physics_.emitPerFrame<<"x"<<physics_.emitProb<<"(60 Hz), cone"<<physics_.emitConeDegrees<<"deg"
           <<"("<<physics_.emitNegProb<<" mirrored), AGING"<<physics_.agingSpeed<<"+/-"<<physics_.agingVariance
           <<", FRICTION"<<physics_.friction<<", GRAVITY"<<physics_.gravity<<", BROWNIAN"<<physics_.brownian
           <<", SPIN"<<physics_.spinTimeScale<<"rad/s, seed"<<kSeed
           <<(materialsLoaded_?"(PARTICLES.mnu)":"(fallback)");
    qInfo()<<"particles: spot"<<spotDirection_<<"SIZE"<<look_.sizeMiddle<<"->"<<look_.sizeFar
           <<"NEAR/FAR FOCUS"<<look_.nearFocus<<"/"<<look_.farFocus
           <<"GLARE"<<look_.glare<<"P1"<<look_.glareP1<<"P2"<<look_.glareP2<<"SCALE"<<look_.glareScale
           <<"SPEC"<<look_.specularPower<<"; look from the clock preset blend, _Color*_Gamma gain"<<kColorGammaGain;
    logSceneReader("particles");
    return true;
}

void Particles::sync(QOpenGLFunctions_3_3_Core& gl,double seconds) {
    // Fixed-step sim: the state is a pure function of the elapsed seconds and the fixed seed, which is
    // what makes --render-clock captures reproducible. A long stall catches up a bounded number of
    // steps per call instead of spawning a burst or blocking the frame.
    refreshLook(); // theme look only; never touches the simulation state
    const long long target=seconds>0.0?static_cast<long long>(std::floor(seconds/physics_.deltaTime)):0;
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
    program_.setUniformValue("uIridescentExp",look_.iridescentExp);
    program_.setUniformValue("uHasIridescent",iridescent_?1.f:0.f);
    program_.setUniformValue("uIridescent",0);
    program_.setUniformValue("uSpotDir",spotDirection_);
    program_.setUniformValue("uSpecularPower",look_.specularPower);
    program_.setUniformValue("uSizeMiddle",look_.sizeMiddle);
    program_.setUniformValue("uSizeFar",look_.sizeFar);
    program_.setUniformValue("uNearFocus",look_.nearFocus);
    program_.setUniformValue("uFarFocus",look_.farFocus);
    program_.setUniformValue("uFresnelExp",look_.fresnel);
    program_.setUniformValue("uLambertCoeff",look_.lambertCoeff);
    program_.setUniformValue("uSpecularCoeff",look_.specularCoeff);
    program_.setUniformValue("uExposure",look_.exposure);
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
    glareProgram_.setUniformValue("uSpotDir",spotDirection_);
    glareProgram_.setUniformValue("uSpecularPower",look_.specularPower);
    glareProgram_.setUniformValue("uSizeMiddle",look_.sizeMiddle);
    glareProgram_.setUniformValue("uSizeFar",look_.sizeFar);
    glareProgram_.setUniformValue("uNearFocus",look_.nearFocus);
    glareProgram_.setUniformValue("uFarFocus",look_.farFocus);
    glareProgram_.setUniformValue("uGlareScale",look_.glareScale);
    glareProgram_.setUniformValue("uFresnelExp",look_.fresnel);
    glareProgram_.setUniformValue("uLambertCoeff",look_.lambertCoeff);
    glareProgram_.setUniformValue("uSpecularCoeff",look_.specularCoeff);
    glareProgram_.setUniformValue("uExposure",look_.exposure);
    glareProgram_.setUniformValue("uGlare",look_.glare);
    glareProgram_.setUniformValue("uGlareP1",look_.glareP1);
    glareProgram_.setUniformValue("uGlareP2",look_.glareP2);
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
