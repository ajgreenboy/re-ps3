#include "particles.h"
#include "assets.h"
#include "qrc.h"
#include "rco_objects.h"
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QImage>
#include <QMatrix4x4>
#include <QtMath>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>
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
constexpr float kSpinStep=1.f/60;          // captured: the kernel advances the spin by SPIN TIME SCALE x 1/60 per
                                           // step (constant 0x3c888888 at 0x070e8), not by DELTA TIME
constexpr int kMaxCatchUpSteps=4096;       // derived: 4096 ticks ~= 68 s of sim, so
                                           // --render-clock 30 reaches t=30 in one call while a long
                                           // stall cannot freeze the app for more than that.
constexpr float kLifeBoxMin[3]={-10.f,-10.f,-12.f}; // traced: data 0x940b0, stored at renderer+0x900
constexpr float kLifeBoxMax[3]={10.f,10.f,7.f};      // traced: data 0x940c0, stored at renderer+0x910
constexpr float kAgeLimit=0.99999f;                  // traced: DAT_00092fe8, the kernel's age test 0x3f7fff58
constexpr unsigned int kSeed=0x5053u;      // fit: fixed seed so --render-clock captures reproduce;
                                           // the firmware's own seed is not traced (Park-Miller itself is)
// --- look / geometry ----------------------------------------------------------------------------
// World units are the firmware's particle units. A 22-unit-tall viewport puts PARTICLES.mnu's
// SIZE MIDDLE (0.0536 in the base file) at a ~2.6 px dot at 1080p, which matches the RPCS3 reference
// captures, and the 0.287..0.394 units/step speeds then cross ~half the screen over a mean lifetime.
constexpr float kScreenHeight=22.f;        // fit: world-units-per-viewport-height fitted to the captures
constexpr float kHalfHeight=kScreenHeight*.5f;
// Emission origin is the one unknown the audit leaves open (6-A2: births come through a PPU command queue
// whose producer is not decompiled). With the ribbon drawn, dust is born on its sheet (spawnParticle); this
// box is the fallback when there is no ribbon: the full width, below the centre line, in a depth band.
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
const float kTanHalfFovy=0.4985816f; // derived: tan(26.5 deg), the particle camera's fovy 53
const QVector3D kCameraEye(0,0,2); // captured: BackgroundView eye (data 0x93730), the particle camera's lookAt
// --- pass 1: particles_quads ---------------------------------------------------------------------
const char* vertexSource=R"(#version 330 core
// particles_quads vertex contract (audit/PARTICLES.md 2.2): float4 position with the per-vertex life
// alpha in w, float2 uv0 billboard corner, float4 per-particle orientation quaternion.
layout(location=0) in vec4 aPos;
layout(location=8) in vec2 aUv;
layout(location=11) in vec4 aRot;
uniform vec2 uHalfExtent;   // world units spanned by half the viewport (screen height = 22 units)
uniform float uGlobalAlpha; // PARTICLES.mnu GLOBAL ALPHA (_Transparency.y)
uniform vec3 uSpotPos;      // PARTICLES.mnu SPOT POS, a point light (_LightPack)
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
    // The spot is a point light: particles_quads.vpo builds normalize(_LightPack position - particle) per vertex
    // (r[4]) and the fpo the same vector per pixel, so dust in front of SPOT POS is lit from behind and dust behind
    // it from the front. The camera looks down the view axis, +z.
    vec3 lightDir=normalize(uSpotPos-aPos.xyz);
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));
    vec4 q=aRot/length(aRot);
    // Depth of field: sharp at NEAR FOCUS, fully defocused at FAR FOCUS (both PARTICLES.mnu). The
    // defocus term only reaches part of the way to SIZE FAR (the fully defocused bound) - the fpo's
    // SIZE->corner mapping is the piece of DoF packing the audit could not recover (6-B3).
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-uNearFocus)/max(uFarFocus-uNearFocus,1e-4),0.0,1.0);
    // Perspective: the dust shares the ribbon's camera (fovy 53, ribbon.cpp), so a SIZE in world units spans
    // halfHeight / (tan(26.5 deg) x depth) of this layer's flat viewport units. Replaces the earlier 1.15 fit.
    float size=mix(uSizeMiddle,uSizeFar,defocus*defocus*0.12)*uHalfExtent.y/(tan(radians(26.5))*max(depth,1.0)); // fit: 0.12 span gain
    // Orientation: the vpo blends the particle's quaternion toward _FrontFacingQuaternion by a factor from
    // _NearControl (SIZE ALIGN / NEAR FUZZINESS / NEAR ALIGN), whose packing is untraced; here the factor is 0,
    // so each flake keeps its own tilt and one seen edge-on draws as a sliver, as the console's dust does.
    vec2 corner=aUv*2.0-1.0;
    vec3 world=aPos.xyz+rotateQuat(q,vec3(corner*size*0.5,0.0));
    gl_Position=vec4(world.x/uHalfExtent.x,world.y/uHalfExtent.y,0.0,1.0);
    vec3 normal=rotateQuat(q,vec3(0.0,0.0,1.0));
    vLambert=max(dot(normal,lightDir),0.0);
    vSpecular=pow(max(dot(normal,halfVector),0.0),uSpecularPower);
    // The iridescent lookup coordinate: the fpo samples at normal_v * 0.5 + 0.5, and normal_v is the
    // view-facing orientation normal, so the sample is one point of the ramp per particle - the dust
    // shifts hue as it spins instead of showing the ramp across the quad.
    vIridescent=normal.xy*0.5+0.5;
    // Alpha chain (vpo lines 186-203, applied per vertex as the program does): fresnel edge term
    // T = 1 - fresnel^FRESNEL, GLOBAL ALPHA and the SPU's per-vertex life alpha. The vpo's life-bounds edge
    // fade is 1 for the plugin's +/-10000 box (FUN_0005d234 from 0x93024/0x93028); its NEAR/FAR DARKNESS and
    // DoF terms are not ported (6-B3), and the fitted depth fade that stood in for them is gone.
    // The vpo's fresnel term is taken against the light vector, not the view axis: 1 - (1 - |L.N|)^FRESNEL.
    float fresnel=1.0-abs(dot(normal,lightDir));
    float T=clamp(1.0-pow(fresnel,uFresnelExp),0.0,1.0);
    vLife=aPos.w*uGlobalAlpha*T;
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
uniform vec3 uSpotPos;
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
    vec3 lightDir=normalize(uSpotPos-aPos.xyz); // the same point light as pass 1
    vec3 halfVector=normalize(lightDir+vec3(0.0,0.0,1.0));
    vec4 q=aRot/length(aRot);
    float depth=-aPos.z;
    float defocus=clamp(abs(depth-uNearFocus)/max(uFarFocus-uNearFocus,1e-4),0.0,1.0);
    // GLARE SCALE is what makes the glints read as bloom seeds rather than dots; the second vpo
    // multiplies the corner extent by _Glare.y (slot 459) - glsl/particles_second.vpo.glsl line 172-175.
    float size=mix(uSizeMiddle,uSizeFar,defocus*defocus*0.12)*uHalfExtent.y/(tan(radians(26.5))*max(depth,1.0))*uGlareScale; // as pass 1
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
    float fresnel=1.0-abs(dot(normal,lightDir));
    float T=clamp(1.0-pow(fresnel,uFresnelExp),0.0,1.0);
    vLife=aPos.w*uGlobalAlpha*T;
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
    // Park-Miller, seed = seed * 16807 mod (2^31-1). Used for births only; the kernel's Brownian draw is its own
    // per-tick reseeded LCG (kernelRandom in step()).
    seed_=unsigned((static_cast<unsigned long long>(seed_)*16807ull)%2147483647ull);
    return float(seed_)*(1.f/2147483647.f);
}

int Particles::takeSlot() {
    if(free_.empty())return -1;
    const int slot=free_.back();free_.pop_back();
    pool_[size_t(slot)].alive=true;++live_;
    return slot;
}

unsigned Particles::stateChecksum() const {
    // FNV-1a over the live pool, for XMB_PARTICLE_LOG determinism checks.
    unsigned hash=2166136261u;
    const auto mix=[&hash](const void* data,size_t bytes){
        const unsigned char* p=static_cast<const unsigned char*>(data);
        for(size_t i=0;i<bytes;++i){hash^=p[i];hash*=16777619u;}
    };
    for(const Particle& p:pool_) {
        if(!p.alive)continue;
        mix(&p.px,8*sizeof(float)); // px,py,pz,vx,vy,vz,age,ageRate
        mix(&p.qx,7*sizeof(float)); // quaternion + spin axis
    }
    return hash;
}

void Particles::trackCursor(int category,int depth,int selection) {
    if(cursorCategory_>=0 && depth==cursorDepth_) {
        // FUN_0000a3e8 sends 0 when from.x < to.x [TRACED]; which way its cell x runs is not traced. The console's latch
        // burst (audit/PARTICLES.md, 2026-11-05) drifts the dust right after a LEFT move, which the traced rotation
        // (state 1 -> axis +Y, Rodrigues, P' = R P) gives for state 0: so a move to a lower column index is state 0.
        if(category!=cursorCategory_)padState_=category<cursorCategory_?0:1;
        else if(selection!=cursorSelection_)padState_=cursorSelection_<selection?2:3;
    }
    cursorCategory_=category;cursorDepth_=depth;cursorSelection_=selection;
}
void Particles::trackWidgets(const std::vector<std::pair<quint64,QPointF>>& screen,double seconds) {
    // FUN_0002f90c projects the widget's world translation through layer+0x1020, which at runtime is the XMB camera
    // at 2700 (rows (2.8125,0,0,0) (0,5,0,0) (0,0,-1.00002,-1) (0,0,2680.05,2700)): on the z = 0 plane that is
    // x / 960 and y / 540 from the screen centre, y up. View hands over screen centres that already carry the
    // parent column's recede, so they are mapped straight to NDC. [TRACED matrix; runtime dump]
    // The renderer draws faster than the 60 Hz tick (~144 fps here), so a tick that took whichever frame came last could
    // carry one and a half or two frames' movement. The last eight sets are kept and each tick samples them by linear
    // interpolation one tick late (stepIconWind), a time that always lies inside the drawn history.
    std::unordered_map<quint64,QPointF> set;
    for(const auto& [key,point]:screen)set.emplace(key,QPointF((point.x()-960.)/960.,(540.-point.y())/540.));
    if(!widgetFrames_.empty() && widgetFrames_.back().first>=seconds)widgetFrames_.back().second=std::move(set);
    else widgetFrames_.emplace_back(seconds,std::move(set));
    while(widgetFrames_.size()>8)widgetFrames_.pop_front();
}
void Particles::stepIconWind() {
    // FUN_0002f90c, once per tracked widget per tick [TRACED, custom_render_plugin; constants read at runtime]:
    //   N = the widget's NDC position; d = N - the N stored at its last visit (a first visit stores N, so d = 0);
    //   a non-zero (dx, dy) writes the ONE cell under N: X = clamp(int((Nx + 1) / 2 * 32), 0, 31),
    //   Y = clamp(int((Ny + 1) / 2 * 16), 0, 15), bytes (trunc(clamp(dx K S0 S1 10, -1, 1) 127),
    //   trunc(clamp(dy S0 S2 10, -1, 1) 127), 0), K = layer+0x14 = 16/9, S0..S2 = ICON WIND / SCL X / SCL Y.
    // The store overwrites the cell (the last widget in it wins). The plugin clears its map above 100 entries;
    // the console tracked 14..23, so that never triggers here. [The runtime grid: only Y bytes, up to +-127 on a
    // list scroll in the item column, +-53 on a category move in the category row.]
    if(widgetFrames_.empty())return;
    // One tick late: the sample time lies inside the drawn frames, so every position is an interpolation (a
    // decelerating ease extrapolated past the newest frame overshot its first step by ~60%).
    const double at=double(step_)/double(kReferenceFrameRate);
    size_t after=0;
    while(after<widgetFrames_.size() && widgetFrames_[after].first<at)++after;
    for(const auto& [key,newest]:widgetFrames_.back().second) {
        QPointF now=newest;
        if(after==0) {
            if(const auto hit=widgetFrames_.front().second.find(key);hit!=widgetFrames_.front().second.end())now=hit->second;
        } else if(after<widgetFrames_.size()) {
            const auto& a=widgetFrames_[after-1];const auto& b=widgetFrames_[after];
            const auto pa=a.second.find(key),pb=b.second.find(key);
            if(pa!=a.second.end() && pb!=b.second.end() && b.first>a.first)
                now=pa->second+(pb->second-pa->second)*((at-a.first)/(b.first-a.first));
            else if(pb!=b.second.end())now=pb->second;
        }
        const auto last=widgetLast_.find(key);
        if(last==widgetLast_.end()){widgetLast_.emplace(key,now);continue;}
        const double dx=now.x()-last->second.x(),dy=now.y()-last->second.y();
        last->second=now;
        if(dx==0. && dy==0.)continue;
        const int cx=std::clamp(int((now.x()+1.)*.5*32.),0,31),cy=std::clamp(int((now.y()+1.)*.5*16.),0,15);
        const auto toByte=[](double v){return qint8(int(std::clamp(v*10.,-1.,1.)*127.));};
        qint8* cell=&flowGrid_[size_t((cy*32+cx)*3)];
        cell[0]=toByte(dx*double(aspect_)*physics_.iconWind*physics_.iconWindSclX);
        cell[1]=toByte(dy*physics_.iconWind*physics_.iconWindSclY);
        cell[2]=0;
    }
}

void Particles::resetSimulation() {
    live_=0;
    step_=0;
    seed_=kSeed;
    padState_=4;
    padRotation_=QVector3D();
    padGustVelocity_=padGain_=0;
    excitePosition_=exciteVelocity_=exciteLevel_=0;
    flowGrid_.fill(0);widgetLast_.clear();
    std::fill(pool_.begin(),pool_.end(),Particle{});
    // The console's first births take slot 2047 and fill downward (pool dumps, audit/PARTICLES.md): the free list
    // holds 0..2047 pushed in order and pops LIFO.
    free_.clear();
    for(int slot=0;slot<int(pool_.size());++slot)free_.push_back(slot);
}

double Particles::dayFraction() const {
    // The same clock View hands to the ribbon: --background-clock overrides the local time of day,
    // otherwise the live clock. View owns that option and does not pass it to this layer, so the
    // layer re-reads the argument (the same pattern as --dev-flash on the no-Assets entry point).
    const QDateTime when=clockOverride_.isValid()?clockOverride_:QDateTime::currentDateTime();
    return double(when.time().msecsSinceStartOfDay())/86400000.0;
}

const std::vector<std::pair<const char*,float Particles::Look::*>>& Particles::lookKeys() {
    static const std::vector<std::pair<const char*,float Look::*>> keys={
        {"size middle",&Look::sizeMiddle},{"size near",&Look::sizeNear},{"size far",&Look::sizeFar},
        {"near focus",&Look::nearFocus},{"near focus_dist",&Look::nearFocusDist},{"near focus_pow",&Look::nearFocusPow},
        {"near darkness",&Look::nearDarkness},{"near fuzziness",&Look::nearFuzziness},
        {"far focus",&Look::farFocus},{"far focus_dist",&Look::farFocusDist},{"far focus_pow",&Look::farFocusPow},
        {"far darkness",&Look::farDarkness},{"near align",&Look::nearAlign},{"size align",&Look::sizeAlign},
        {"glare",&Look::glare},{"glare scale",&Look::glareScale},{"glare p1",&Look::glareP1},{"glare p2",&Look::glareP2},
        {"specular power",&Look::specularPower},{"specular coeff",&Look::specularCoeff},{"lambert coeff",&Look::lambertCoeff},
        {"exposure",&Look::exposure},{"fresnel",&Look::fresnel},{"iridescent exp",&Look::iridescentExp},
        {"color_control",&Look::colorControl},{"global alpha",&Look::globalAlpha}};
    return keys;
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
    Look out;
    for(const auto& [key,member]:lookKeys())out.*member=A.*member+(B.*member-A.*member)*float(f);
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
        for(const auto& [key,member]:lookKeys())lookBase_.*member=float(base.number(QString::fromLatin1(key),lookBase_.*member));
        materialsLoaded_=true;
    }
    // PARTICLES_UI.mnu: the input wind's keys (the d-pad rotation and the brownian boost it excites).
    if(const Materials::Mnu& ui=materials.particlesUi();ui.ok) {
        ++files;
        const auto key=[&](const char* name,float& out){out=float(ui.number(QString::fromLatin1(name),out));};
        key("brownian",physics_.uiBrownian);
        key("dpad rot max",physics_.dpadRotMax);
        key("dpad scale x",physics_.dpadScaleX);
        key("dpad scale y",physics_.dpadScaleY);
        key("icon wind",physics_.iconWind);
        key("icon wind scl x",physics_.iconWindSclX);
        key("icon wind scl y",physics_.iconWindSclY);
    } else {
        qWarning().noquote()<<"particles: PARTICLES_UI.mnu unavailable; the d-pad wind uses the 4.93 fallbacks";
    }
    if(base.ok) {
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
        for(const auto& [key,member]:lookKeys())out.*member=float(mnu.number(QString::fromLatin1(key),out.*member));
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
    spotPosition_=QVector3D(float(sx),float(sy),float(sz));
    if(base.ok)spotAttenuation_=QVector3D(float(base.number(QStringLiteral("spot attn x"),1.)),
        float(base.number(QStringLiteral("spot attn y"),0.)),float(base.number(QStringLiteral("spot attn z"),0.)));
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

// FUN_00030b84, one queued cell: the particle is born exactly at the sheet point P and sent along a cone around the
// point's own motion. V = frame displacement / DELTA TIME (the plugin divides the 60 Hz step by holder+0x1398); a
// still point (|V| < 1e-4) spawns nothing. The axis a = V/|V| is negated with EMIT NEG PROB; the cone's polar angle is
// uniform in [0, EMIT CONE ANGLE] (not per solid angle) around a, in the frame (T, a x T) with T the longest of a x the
// unit axes; the component along the camera's view axis is scaled by EMIT VEL ZSCALE (0: removed) and the vector is
// not renormalised; speed = max(EMIT VEL MIN, sqrt(|V| EMIT VEL MUL (1 + r EMIT VEL VAR))), r in (-1, 1]; aging rate
// max(0.001, AGING SPEED (1 + r AGING VARIANCE)). [TRACED 0x30b84-0x31494; the frame displacement uses the sheet's
// velocity at render time, since the native has no sheet history inside one catch-up.]
void Particles::spawnFromGrid(int column,int row) {
    const size_t at=size_t(row)*size_t(emitterSide_)+size_t(column);
    const QVector3D V=emitterVelocity_[at]*float((1./kReferenceFrameRate)/physics_.deltaTime);
    const float speedOfPoint=V.length();
    // XMB_PARTICLE_SPAWN_LOG=<file>: each queued cell's P and V, for comparison with the console's emitter queue.
    static FILE* spawnLog=[]{const QByteArray path=qgetenv("XMB_PARTICLE_SPAWN_LOG");return path.isEmpty()?nullptr:std::fopen(path.constData(),"w");}();
    if(spawnLog)std::fprintf(spawnLog,"%lld %d %d %g %g %g %g %g %g\n",step_,column,row,emitter_[at].x(),emitter_[at].y(),emitter_[at].z(),V.x(),V.y(),V.z());
    if(speedOfPoint<1e-4f || free_.empty())return;
    QVector3D axis=V/speedOfPoint;
    if(random()<physics_.emitNegProb)axis=-axis;
    QVector3D tangent;
    for(const QVector3D& unit:{QVector3D(1,0,0),QVector3D(0,1,0),QVector3D(0,0,1)}) {
        const QVector3D t=QVector3D::crossProduct(axis,unit);
        if(t.lengthSquared()>tangent.lengthSquared())tangent=t;
    }
    tangent.normalize();
    const QVector3D bitangent=QVector3D::crossProduct(axis,tangent);
    const float theta=random()*physics_.emitConeDegrees*(3.14159265f/180.f),phi=random()*6.2831853f;
    QVector3D direction=tangent*(std::sin(theta)*std::cos(phi))+bitangent*(std::sin(theta)*std::sin(phi))+axis*std::cos(theta);
    direction.setZ(direction.z()*physics_.emitVelZScale); // the camera looks down -z in this world
    const float speed=std::max(physics_.emitVelMin,
        std::sqrt(std::max(0.f,speedOfPoint*physics_.emitVelMul*(1.f+(2.f*random()-1.f)*physics_.emitVelVar))));
    Particle& p=pool_[size_t(takeSlot())];
    const QVector3D& P=emitter_[at];
    p.px=P.x();p.py=P.y();p.pz=P.z();
    p.vx=direction.x()*speed;p.vy=direction.y()*speed;p.vz=direction.z()*speed;
    p.age=0.f;
    p.ageRate=std::max(0.001f,physics_.agingSpeed*(1.f+(2.f*random()-1.f)*physics_.agingVariance));
    orientParticle(p);
}

// No ribbon: the fitted emission volume (kEmit*, flat viewport units at a camera depth) moved into the world, with the
// earlier screen-plane cone. Only reached without a ribbon sheet.
void Particles::spawnInBox() {
    if(free_.empty())return;
    Particle& p=pool_[size_t(takeSlot())];
    const float depth=-(kEmitZNear+random()*(kEmitZFar-kEmitZNear));
    const float toView=kTanHalfFovy*depth/kHalfHeight;
    p.px=(2.f*random()-1.f)*kHalfHeight*aspect_*kEmitXMargin*toView;
    p.py=(kEmitYCentre+(random()+random()-1.f)*kEmitYSpread)*toView;
    p.pz=kCameraEye.z()-depth;
    const float speed=physics_.emitVelMin+physics_.emitVelMul*(1.f+physics_.emitVelVar*(2.f*random()-1.f));
    const float angle=(2.f*random()-1.f)*physics_.emitConeDegrees*(3.14159265f/180.f);
    const float mirror=random()<physics_.emitNegProb?-1.f:1.f;
    p.vx=mirror*speed*std::cos(angle);
    p.vy=mirror*speed*std::sin(angle);
    p.vz=0.f;
    p.age=0.f;
    p.ageRate=physics_.agingSpeed*(1.f+physics_.agingVariance*(2.f*random()-1.f));
    orientParticle(p);
}

// The orientation the type 3 command would set is not traced (its pusher is not in FUN_00030b84), so a fresh particle
// gets a random unit quaternion whose axis also carries the spin.
void Particles::orientParticle(Particle& p) {
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
    // Emission is the PPU's, once per 60 Hz frame (custom_render_plugin FUN_0002dde4): one EMIT PROB draw gates the
    // frame, and a passing frame queues int(EMIT PER FRAME) random grid cells, (int(u (dimX - 1)), int(u (dimY - 1))),
    // that FUN_00030b84 turns into particles (spawnFromGrid). The SPU kernel never spawns. [TRACED; the int conversion
    // of 16.6539 as truncation is inferred.]
    // One step is one 60 Hz tick: FUN_00062464 flushes the command ring and dispatches the kernel once, and the kernel
    // has no sub-step loop. [TRACED] A birth into a full pool is dropped (FUN_00059228 pops the free list or returns -1,
    // FUN_0005b6bc writes nothing). [TRACED]
    if(random()<physics_.emitProb) {
        const int candidates=int(physics_.emitPerFrame);
        const bool grid=emitterSide_>1 && emitter_.size()==size_t(emitterSide_)*emitterSide_ && emitterVelocity_.size()==emitter_.size();
        for(int k=0;k<candidates;++k) {
            if(grid) {
                const int column=int(random()*float(emitterSide_-1)),row=int(random()*float(emitterSide_-1));
                spawnFromGrid(column,row);
            } else spawnInBox();
        }
    }
    // particles.elf 0x06f64-0x07098, semi-implicit Euler: vel += (gravity + BROWNIAN x R - FRICTION x vel) x k, then
    // pos += vel x k (R in [-1, 1)^3 from the Park-Miller call at 0x06fe4). Both updates multiply by the same physics
    // vector at +0x8b0 (lqd 2224($87) at 0x7080 and 0x7090). FUN_0005d510 initialises it to 1/60, but the PPU tick
    // FUN_00031494 splats the parameter struct's DELTA TIME (sim+0x1360 +0x38, the mnu key order) into it every tick
    // (stvx at 0x319ac), with w = 1 so age adds a bare AGING. [TRACED] The idle kernel's other terms are zero: the pad
    // pivot impulse (0x7064, M92 identity without pad input), the constant acceleration V890 (layer+0x680, never
    // written) and the flow field, which is zero until an icon moves (the icon wind below; no WIND SCALE gate).
    const float dt=float(physics_.deltaTime);
    // The icon wind into the flow field, once per tick before the kernel: every cell steps 1 toward 0 (the console's
    // grid falls by exactly 1 per tick - 36 -> 21 -> 6 over 15-tick dumps - and neither the SPU kernel nor the
    // plugin's tick reads as that decay, so its writer is not traced; the rate is measured), then the widgets that
    // moved since the last tick write their cells (stepIconWind).
    bool windy=false;
    for(auto& byte:flowGrid_){if(byte>0)--byte;else if(byte<0)++byte;windy|=byte!=0;}
    stepIconWind();
    if(!windy)windy=std::any_of(flowGrid_.begin(),flowGrid_.end(),[](qint8 v){return v!=0;});
    // Local diagnosis: XMB_FLOW_LOG=1 prints the non-zero cells every 15 ticks, the console dumps' cadence
    // (work/flow-grid.ts prints the same form from an RPCS3 run).
    if(static const bool flowLog=qEnvironmentVariableIsSet("XMB_FLOW_LOG");flowLog && windy && step_%15==0) {
        QStringList hot;int cells=0,peak=0;
        for(int c=0;c<512;++c) {
            const int y=flowGrid_[size_t(c*3+1)],x=flowGrid_[size_t(c*3)];
            if(!x && !y && !flowGrid_[size_t(c*3+2)])continue;
            ++cells;peak=std::max(peak,std::abs(y));
            if(hot.size()<6)hot<<QStringLiteral("(%1,%2)=%3/%4/0").arg(c%32).arg(c/32).arg(x).arg(y);
        }
        qInfo().noquote()<<QStringLiteral("flow: step %1: %2 cells, max y %3 %4").arg(step_).arg(cells).arg(peak).arg(hot.join(' '));
    }
    // The input wind, once per tick before the kernel (FUN_0002d02c, FUN_00031494). [TRACED unless marked]
    // A pressed direction (dx, dy) sets the rotation vector R = (SCALE Y * -dy, SCALE X * dx, 0) - in the particle
    // camera's view basis, which is the world basis here (the lookAt runs down -z with y up) - and adds 0.03 to the
    // gust smoother and 0.04 |Rx + Ry + Rz| to the brownian excitation. The direction table's signs (data 0x93ea0)
    // are read as left (-1, 0), right (1, 0), up (0, 1), down (0, -1). [INFERRED signs]
    // The state is a one-shot event: every tick writes 4 back (0x2d4f0-0x2d500 is reached on both sides of the 1e-5
    // test), so each cursor move adds the input (layer+0x190 = 0.03, 0x2d35c-0x2d368) exactly once, and R stays at
    // the last move's direction while g decays. [TRACED at runtime: audit/PARTICLES.md, the wind's runtime trace]
    float gustInput=0.f,exciteInput=0.f;
    if(padState_>=0 && padState_<4) {
        static constexpr float kDirection[4][2]={{-1.f,0.f},{1.f,0.f},{0.f,1.f},{0.f,-1.f}};
        const float dx=kDirection[padState_][0],dy=kDirection[padState_][1];
        padRotation_=QVector3D(physics_.dpadScaleY*-dy,physics_.dpadScaleX*dx,0.f);
        gustInput=0.03f;
        exciteInput=0.04f*std::abs(padRotation_.x()+padRotation_.y()+padRotation_.z());
    }
    padState_=4;
    padGustVelocity_=0.98f*padGustVelocity_+gustInput;
    padGain_=std::clamp(padGustVelocity_,0.f,1.f);
    // Brownian excitation: v' = 0.6 v - 0.004 x + input, x' = x + v' (data 0x92444 / 0x92448), level clamp(x', 0, 1);
    // the kernel's Brownian scale is BROWNIAN SCALE + PARTICLES_UI BROWNIAN x level (+ RSHAKE BRW x the sixaxis
    // history step, which this build has no sensor for).
    exciteVelocity_=0.6f*exciteVelocity_-0.004f*excitePosition_+exciteInput;
    excitePosition_+=exciteVelocity_;
    exciteLevel_=std::clamp(excitePosition_,0.f,1.f);
    // The rotation M92 = rot(v / |v|, |v|) with v = DPAD ROT MAX x g x R, about the pivot eye - 2 normalize(eye -
    // centre) = (0, 0, 0); below 1e-5 it is the identity. The kernel adds the rotated point's offset / dt to each
    // velocity (particles.elf 0x7040-0x7064).
    const QVector3D spin=physics_.dpadRotMax*padGain_*padRotation_;
    const float angle=spin.length();
    const bool rotating=angle>=1e-5f;
    const QVector3D pivot=kCameraEye-2.f*(kCameraEye-QVector3D(0,0,-7)).normalized();
    QMatrix4x4 rotation;
    if(rotating)rotation.rotate(qRadiansToDegrees(angle),spin/angle);
    const float drag=1.f-physics_.friction*dt;
    const float kick=(physics_.brownian+physics_.uiBrownian*exciteLevel_)*dt;
    // Quaternion spin: q = spinStep * q, SPIN TIME SCALE radians per second.
    const float halfSpin=.5f*physics_.spinTimeScale*kSpinStep;
    const float spinC=std::cos(halfSpin),spinS=std::sin(halfSpin);
    // The kernel's Brownian R (particles.elf func_33d8 / func_30e8): three LCG streams s' = s * 16807 mod 2^32, each
    // component (0x40000000 | s' >> 9) as a float minus 3, uniform in [-1, 1). The streams are reseeded every tick from
    // the constants 0x98756161 / 0x21324889 / 0x82181158 (func_4978 0x49a8-0x49dc, stored at 0x8224) and a draw is
    // taken only for a live record (0x6f6c), walking the slots in order, so a slot keeps its R from tick to tick until
    // a slot before it is born or dies. [TRACED; the x/y/z lane order of the three streams is inferred]
    unsigned int stream[3]={0x98756161u,0x21324889u,0x82181158u};
    const auto kernelRandom=[&stream](int k){
        stream[k]*=16807u;
        const unsigned int bits=0x40000000u|(stream[k]>>9);
        float value;std::memcpy(&value,&bits,sizeof(value));
        return value-3.f;
    };
    for(int i=0;i<int(pool_.size());++i) {
        Particle& p=pool_[size_t(i)];
        if(!p.alive)continue;
        if(rotating) {
            const QVector3D P(p.px,p.py,p.pz),turned=pivot+rotation.map(P-pivot),impulse=(turned-P)/dt;
            p.vx+=impulse.x();p.vy+=impulse.y();p.vz+=impulse.z();
        }
        // Brownian velocity kick, one RNG draw per axis (audit 3.3), gravity and friction, all scaled by dt.
        const float rx=kernelRandom(0),ry=kernelRandom(1),rz=kernelRandom(2);
        p.vx+=kick*rx;
        p.vy+=kick*ry+physics_.gravity*dt;
        p.vz+=kick*rz;
        // The flow-field term (particles.elf 0x6fa4-0x7018, in the same dt bracket as the kick): s_a0 x M94 .
        // bilinear(grid / 127, (M95 . [p, 1]).xy x (32, 16) - 0.5), corner indices clamped to the grid. Runtime
        // values (sim+0x800..0x8bf, sim+0x9a0): M95 = scale (0.0626836, 0.111427) + 0.5, i.e. world x +-7.977 and
        // y +-4.487 - the screen at the dust's z = -7 plane - onto [0, 1]; M94 = its inverse scale (15.9546,
        // 8.97447, 1), applied to a w = 0 sample so its translation drops out; s_a0 = 1. [TRACED + runtime dump]
        if(windy) {
            const float u=(0.0626836f*p.px+.5f)*32.f-.5f,v=(0.111427f*p.py+.5f)*16.f-.5f;
            const float fu=std::floor(u),fv=std::floor(v),wu=u-fu,wv=v-fv;
            const int x0=std::clamp(int(fu),0,31),x1=std::clamp(int(fu)+1,0,31);
            const int y0=std::clamp(int(fv),0,15),y1=std::clamp(int(fv)+1,0,15);
            float g[3];
            for(int k=0;k<3;++k) {
                const auto at=[&](int x,int y){return float(flowGrid_[size_t((y*32+x)*3+k)])/127.f;};
                const float bottom=at(x0,y0)+(at(x1,y0)-at(x0,y0))*wu,top=at(x0,y1)+(at(x1,y1)-at(x0,y1))*wu;
                g[k]=bottom+(top-bottom)*wv;
            }
            p.vx+=15.9546f*g[0]*dt;
            p.vy+=8.97447f*g[1]*dt;
            p.vz+=g[2]*dt;
        }
        p.vx*=drag;
        p.vy*=drag;
        p.vz*=drag;
        p.px+=p.vx*dt;
        p.py+=p.vy*dt;
        p.pz+=p.vz*dt;
        p.age+=p.ageRate;
        // The kernel recycles a record past age 0.99999 or outside the life box (particles.elf 0x7178-0x719c: the age
        // test, then a clamp-equals-point box test; the box is renderer+0x900, the same one the vpo fades against).
        const bool outside=p.px<kLifeBoxMin[0]||p.py<kLifeBoxMin[1]||p.pz<kLifeBoxMin[2]||
                           p.px>kLifeBoxMax[0]||p.py>kLifeBoxMax[1]||p.pz>kLifeBoxMax[2];
        if(p.age>=kAgeLimit||outside) { // dead: the slot goes back on the free list
            p.alive=false;--live_;free_.push_back(i);
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

// The hand-written fallback programs were written for flat viewport units (kScreenHeight tall at every depth, z = -camera
// depth); the records are now world positions, so their vertex input is mapped back at the top of main().
static QString fallbackVertex(const char* source) {
    QString s=QString::fromLatin1(source);
    s.replace("layout(location=0) in vec4 aPos;","layout(location=0) in vec4 aPosWorld_;\nvec4 aPos;");
    s.replace("void main(){",QStringLiteral("void main(){\n    { float d=%1-aPosWorld_.z; aPos=vec4(aPosWorld_.xy*(%2/(%3*max(d,1e-3))),-d,aPosWorld_.w); }")
        .arg(double(kCameraEye.z())).arg(double(kHalfHeight)).arg(double(kTanHalfFovy),0,'g',9));
    return s;
}

bool Particles::initPrograms() {
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,fallbackVertex(vertexSource)) ||
       !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentSource) || !program_.link()) {
        qWarning().noquote()<<"particles: particles_quads link failed:"<<program_.log();
        return false;
    }
    if(!glareProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex,fallbackVertex(glareVertexSource)) ||
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
    initFirmwarePass(assets,0);
    initFirmwarePass(assets,1);
    qInfo()<<"particles: firmware pool"<<kCapacity<<"records, DELTA TIME"<<physics_.deltaTime<<"s,"
           <<"EMIT"<<physics_.emitPerFrame<<"x"<<physics_.emitProb<<"(60 Hz), cone"<<physics_.emitConeDegrees<<"deg"
           <<"("<<physics_.emitNegProb<<" mirrored), AGING"<<physics_.agingSpeed<<"+/-"<<physics_.agingVariance
           <<", FRICTION"<<physics_.friction<<", GRAVITY"<<physics_.gravity<<", BROWNIAN"<<physics_.brownian
           <<", SPIN"<<physics_.spinTimeScale<<"rad/s, seed"<<kSeed
           <<(materialsLoaded_?"(PARTICLES.mnu)":"(fallback)")
           <<"; firmware programs: particles_quads"<<(firmwareReady_[0]?"yes":"no (fallback)")<<", particles_second"<<(firmwareReady_[1]?"yes":"no (fallback)");
    qInfo()<<"particles: spot at"<<spotPosition_<<"SIZE"<<look_.sizeMiddle<<"->"<<look_.sizeFar
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
    const long long target=seconds>0.0?static_cast<long long>(std::floor(seconds*kReferenceFrameRate)):0;
    if(target<step_)resetSimulation(); // the clock went backwards: replay from the seed
    for(int n=0;step_<target && n<kMaxCatchUpSteps;++n) {
        step();
        // XMB_PARTICLE_POOL_DUMP=<path prefix>: the live pool (x y z vx vy vz age slot) every XMB_PARTICLE_POOL_EVERY steps
        // (default 120), for comparison with the console pool read by the RPCS3 PC watch's bulk dump (audit/PARTICLES.md).
        static const QString poolDump=qEnvironmentVariable("XMB_PARTICLE_POOL_DUMP");
        static const int poolEvery=std::max(1,qEnvironmentVariableIntValue("XMB_PARTICLE_POOL_EVERY")>0?qEnvironmentVariableIntValue("XMB_PARTICLE_POOL_EVERY"):120);
        if(!poolDump.isEmpty() && step_%poolEvery==0) {
            QFile file(QString("%1_%2.txt").arg(poolDump).arg(step_));
            if(file.open(QIODevice::WriteOnly|QIODevice::Text)) {
                QTextStream out(&file);
                for(size_t s=0;s<pool_.size();++s) {
                    const Particle& p=pool_[s];
                    if(p.alive)out<<p.px<<' '<<p.py<<' '<<p.pz<<' '<<p.vx<<' '<<p.vy<<' '<<p.vz<<' '<<p.age<<' '<<s<<'\n';
                }
            }
        }
    }
    // Per-vertex life alpha: the SPU envelope, full until 0.94 then linear to 0 at 1.0, with the
    // kernel's 0.02 start clamp.
    const auto lifeEnvelope=[](float age){
        const float in=std::min(age/kLifeIn,1.f);
        const float out=1.f-std::max(age-kLifeFull,0.f)/kLifeFade;
        return std::clamp(in,0.f,1.f)*std::clamp(out,0.f,1.f);
    };
    Vertex* const out=vertices_.data();
    int i=0;
    for(const Particle& p:pool_) {
        if(!p.alive)continue;
        const float life=lifeEnvelope(p.age);
        for(int corner=0;corner<4;++corner) {
            Vertex& v=out[size_t(i)*4+corner];
            v.x=p.px;v.y=p.py;v.z=p.pz;v.life=life;
            v.u=kCornerU[corner];v.v=kCornerV[corner];v.pad0=0.f;v.pad1=0.f;
            v.qx=p.qx;v.qy=p.qy;v.qz=p.qz;v.qw=p.qw;
        }
        ++i;
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
    for(const Particle& p:pool_) {
        if(!p.alive)continue;
        if(!std::isfinite(p.px)||!std::isfinite(p.py)||!std::isfinite(p.pz)||
           !std::isfinite(p.vx)||!std::isfinite(p.vy)||!std::isfinite(p.vz)||
           !std::isfinite(p.age)||!std::isfinite(p.qx)||!std::isfinite(p.qy)||
           !std::isfinite(p.qz)||!std::isfinite(p.qw))++nonFinite;
    }
    qInfo().noquote()<<"particles:"<<pass<<"step"<<step_<<"live"<<live_
        <<"nonfinite"<<nonFinite<<"checksum"<<QString::number(stateChecksum(),16)
        <<"target"<<width<<"x"<<height<<"lut"<<(iridescentFromFirmware_?"tga":"fallback");
}

// The simulation runs in the particle camera's world, so the firmware vpo reads the records as they are. The camera is
// the BackgroundView the
// particle draw passes to FUN_000255a4 (mode 1, lookAt set B): constructed (FUN_00024a68) with eye (0, 0, 2), centre
// (0, 0, -7), up (0, 1, 0), fovy 0.9250245 rad, near 0.1, far 1000 - so world = camera space + (0, 0, 2), _Modelview is
// that lookAt (a translation by -2 in z), and _FrontFacingQuaternion the identity rotation.
void Particles::initFirmwarePass(Assets& assets,int pass) {
    const QString name=pass==0?"particles_quads":"particles_second";
    CgProgram& vertexCg=firmwareVertex_[pass];
    CgProgram& fragmentCg=firmwareFragment_[pass];
    QOpenGLShaderProgram& program=firmwareProgram_[pass];
    QString error;
    try {
        vertexCg=CgProgram::load(assets.qglFile("lines","lib/particles/"+name+".vpo"),&error);
        if(vertexCg.valid())fragmentCg=CgProgram::load(assets.qglFile("lines","lib/particles/"+name+".fpo"),&error);
    } catch(const std::exception& e) { error=e.what(); }
    if(!vertexCg.valid() || !fragmentCg.valid()) {
        qWarning().noquote()<<"particles: firmware"<<name<<"unavailable ("<<error<<"); pass"<<pass+1<<"uses the fallback program";
        return;
    }
    if(!program.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexCg.glsl) ||
       !program.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentCg.glsl) || !program.link()) {
        qWarning().noquote()<<"particles: firmware"<<name<<"link failed:"<<program.log();
        return;
    }
    firmwareReady_[pass]=!qEnvironmentVariableIsSet("XMB_PARTICLES_FALLBACK");
}

void Particles::drawFirmwarePass(QOpenGLFunctions_3_3_Core& gl,int pass,int pixelWidth,int pixelHeight) {
    const CgProgram& vertexCg=firmwareVertex_[pass];
    const CgProgram& fragmentCg=firmwareFragment_[pass];
    QOpenGLShaderProgram& program=firmwareProgram_[pass];
    // Vertex constants exactly as FUN_0005c254 (custom_render_plugin 0x5c254) packs them from the renderer's PARTICLES.mnu
    // fields (audit/PARTICLES.md "The firmware particle programs"); both passes get the same values.
    std::vector<QVector4D> vc(468);
    for(const auto& p:vertexCg.parameters)
        if(p.resourceIndex>=0 && p.resourceIndex<468 && !p.defaults.isEmpty())
            vc[size_t(p.resourceIndex)]=QVector4D(p.defaults.value(0),p.defaults.value(1),p.defaults.value(2),p.defaults.value(3));
    const auto setV=[&](const char* name,const QVector4D& v){const int i=vertexCg.parameters.value(name).resourceIndex;if(i>=0&&i<468)vc[size_t(i)]=v;};
    constexpr float fovy=53.f*3.14159265f/180.f; // the ribbon camera (ribbon.cpp, captured)
    const float aspect=float(pixelWidth)/float(pixelHeight);
    QMatrix4x4 projection;projection.perspective(53.f,aspect,.1f,1000.f);
    QMatrix4x4 modelview;modelview.lookAt(kCameraEye,QVector3D(0,0,-7),QVector3D(0,1,0)); // BackgroundView set B (0x93730..)
    projection*=modelview;
    for(int r=0;r<4;++r) {
        setV(QStringLiteral("_ModelviewProjection[%1]").arg(r).toLatin1().constData(),projection.row(r));
        setV(QStringLiteral("_Modelview[%1]").arg(r).toLatin1().constData(),modelview.row(r));
    }
    // _LightPack: rows (eye), (spot pos, specular power), (spot attn), (lambert, specular coeff, exposure, color_control),
    // stored as columns - the vpo reads the eye from vc[264..266].x, and the fpo's slot table (fc 1/10/3 the eye,
    // 2/4/0 the spot, 28/26/25 the attenuation, 29/24/30 the coefficients, 16.y specular power, 6.w color_control)
    // only resolves with that transpose. The fpo lights a flake as |N.L| x fc29.w + (N.H)^power x fc24.w x iridescent,
    // so fc29 (row 0 .w) carries LAMBERT COEFF and fc24 (row 1 .w) the iridescent-tinted SPECULAR COEFF. The setter's
    // registration-index reading had the two names the other way round; that order lit every flake evenly with no
    // glints, while this one reproduces the console's coloured glint tail with _Color at its initialiser
    // (audit/PARTICLES.md "The particle glints").
    const Look& l=look_;
    const QVector4D pack[4]={
        QVector4D(kCameraEye.x(),spotPosition_.x(),spotAttenuation_.x(),l.lambertCoeff),
        QVector4D(kCameraEye.y(),spotPosition_.y(),spotAttenuation_.y(),l.specularCoeff),
        QVector4D(kCameraEye.z(),spotPosition_.z(),spotAttenuation_.z(),l.exposure),
        QVector4D(1,l.specularPower,0,l.colorControl)};
    for(int r=0;r<4;++r)setV(QStringLiteral("_LightPack[%1]").arg(r).toLatin1().constData(),pack[r]);
    setV("_ParticleSize",QVector4D(l.sizeMiddle,l.sizeNear,l.sizeFar,0));
    setV("_Transparency",QVector4D(l.fresnel,l.globalAlpha*globalAlpha_,0,0));
    setV("_Focus",QVector4D(l.nearFocus,l.nearFocus+l.nearFocusDist,l.farFocus,l.farFocus+l.farFocusDist));
    // _FocusCurves.z is the float FUN_0005de2c copies from the camera module, whose getter also feeds tan(0.5 x): the
    // vertical field of view in radians. [TRACED source; the value is the ribbon camera's fovy.]
    setV("_FocusCurves",QVector4D(l.nearFocusPow,l.farFocusPow,fovy,0));
    setV("_NearControl",QVector4D(l.sizeAlign,l.nearFuzziness,l.nearAlign,0));
    setV("_Darkness",QVector4D(l.nearDarkness,l.farDarkness,0,0));
    setV("_FrontFacingQuaternion",QVector4D(0,0,0,1));
    // The life box: a constant world box (FUN_00031494 stores 0x940b0 / 0x940c0 at renderer+0x900/+0x910), in the same
    // world as the camera above; the vpo fades a particle over the last 0.2 units inside each face.
    setV("_LifeBoundsMin",QVector4D(kLifeBoxMin[0],kLifeBoxMin[1],kLifeBoxMin[2],0));
    setV("_LifeBoundsMax",QVector4D(kLifeBoxMax[0],kLifeBoxMax[1],kLifeBoxMax[2],0));
    QVector<QVector4D> fc=fragmentCg.constants;
    const auto setF=[&](const char* name,const QVector4D& v){for(int slot:fragmentCg.parameters.value(name).constantSlots)if(slot<fc.size())fc[slot]=v;};
    for(int r=0;r<4;++r)setF(QStringLiteral("_LightPack[%1]").arg(r).toLatin1().constData(),pack[r]);
    setF("_NearControl",QVector4D(l.sizeAlign,l.nearFuzziness,l.nearAlign,0));
    setF("_IridescentExp",QVector4D(l.iridescentExp,l.iridescentExp,l.iridescentExp,l.iridescentExp));
    // _Color is one grey (g, g, g, 0) the shell can push through FUN_0001afdc from a state-machine message; the steady-state
    // message is not traced, so it keeps its initialiser (1, 1, 1 at 0x60c80). With the light pack above that reproduces
    // the console's glint tail (48+..128+ contrast bins 37/16/10/22 against 33/18/10/20). [TRACED initialiser.]
    setF("_Color",QVector4D(1,1,1,0));
    // _Gamma (renderer+0x1350) is 1.0 from construction (0x60c98) until the shell sends one; FUN_00060660 skips the draw
    // at 0, so it acts as a fade. [TRACED initialiser.]
    setF("_Gamma",QVector4D(1,1,1,1));
    // _Glare, particles_second only (vpo slot 459, fpo slots 27/16/13): (GLARE, GLARE SCALE, GLARE P1, GLARE P2). The vpo
    // scales the quad by .y; the fpo's halo is .x * exp(-.w * r^.z) on the centred uv radius. [TRACED x, y at
    // 0x5c5a8-0x5c5d4 (renderer+0x1438..0x1444 in key order); z, w by the fpo's use of them.]
    const QVector4D glare(l.glare,l.glareScale,l.glareP1,l.glareP2);
    setV("_Glare",glare);
    setF("_Glare",glare);
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glEnable(GL_BLEND);
    gl.glBlendFunc(GL_ONE,GL_ONE); // FUN_00060660: ONE/ONE, FUNC_ADD for both passes; the fpo's colour carries the alpha
    gl.glBindVertexArray(vao_);
    if(iridescent_)iridescent_->bind(0);
    program.bind();
    program.setUniformValueArray("vc",vc.data(),int(vc.size()));
    program.setUniformValueArray("fc",fc.constData(),fc.size());
    program.setUniformValue("tex0",0);
    gl.glDrawElements(GL_TRIANGLES,GLsizei(size_t(live_)*6),GL_UNSIGNED_INT,reinterpret_cast<const void*>(indexOffset_));
    program.release();
    if(iridescent_)iridescent_->release(0);
    gl.glBindVertexArray(0);
    gl.glDisable(GL_BLEND);
}

void Particles::drawQuads(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight) {
    if(live_<=0)return;
    if(firmwareReady_[0]) {
        // FUN_00060660: pass 1 then pass 2 (holder field 0x75 = 2 draws both), same target, ONE/ONE.
        drawFirmwarePass(gl,0,pixelWidth,pixelHeight);
        if(firmwareReady_[1])drawFirmwarePass(gl,1,pixelWidth,pixelHeight);
        return;
    }
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
    program_.setUniformValue("uSpotPos",spotPosition_);
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
    // With the firmware programs, pass 2 was already drawn into the scene by drawQuads: FUN_00060660 draws both passes
    // back to back into the one bound target (no framebuffer bind between them), so the glare source gets the dust only
    // through the scene, like everything else.
    if(firmwareReady_[1])return;
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
    glareProgram_.setUniformValue("uSpotPos",spotPosition_);
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
