#include "particles.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <QDebug>
#include <QMatrix4x4>

namespace xmb {
namespace {
// PARTICLES.mnu (override/day; night differs only in size middle and far focus distance).
constexpr float emitVelMin=.15064f,emitVelMul=.19f,emitVelVar=.282567f,emitConeAngle=51.8695f,emitNegProb=.173899f;
constexpr float emitPerFrame=16.6539f,emitProb=.479115f,agingSpeed=.00285223f,agingVariance=.493003f,friction=.030551f;
constexpr float spinTimeScale=2.74f,deltaTime=.0088883f,gravity=-6.8e-05f,brownianScale=.225311f;
constexpr float spotX=4.16f,spotY=2.63f,spotZ=-7.6f,spotAttn[3]={1,0,0};
constexpr float specularPower=35.2904f,specularCoeff=74.74f,lambertCoeff=8.f,exposure=.0390458f,fresnel=1.33319f;
constexpr float colorControl=1.f,iridescentExp=1.f,globalAlpha=1.f;
constexpr float sizeMiddleDay=.0464771f,sizeMiddleNight=.0536233f,sizeNear=.0772033f,sizeFar=.874062f;
constexpr float nearFocus=6.12435f,nearFocusDist=1.3193f,nearFocusPow=2.47206f,nearDarkness=6.24028f,nearFuzziness=3.79f;
constexpr float farFocus=12.8327f,farFocusDistDay=4.02735f,farFocusDistNight=4.09678f,farFocusPow=1.83324f,farDarkness=13.9f;
constexpr float nearAlign=.722f;
constexpr float glareDay=.201367f,glareNight=.159705f,glareScale=5.44386f,glareP1=.99f,glareP2=4.44f;
// Reconstructed (the SPU simulation and the CPU uniform setters are not traced):
constexpr float sceneScale=3.2f;     // firmware units per view unit: puts the ribbon (view z = -3) near the focus band
constexpr int maxParticles=500;
constexpr float emitRate=.15f;       // share of EMIT PER FRAME x EMIT PROB actually emitted: ~400 alive, a sparse cloud (fitted by eye)
constexpr float emitScatter=1.f;      // firmware units around the emission point (particles fill a volume, not the line)
constexpr float darknessAngle=1.5f;  // _FocusCurves.z: divides the angular-size term of near/far darkness
constexpr float brownianKick=.25f;   // velocity kick per step per unit BROWNIAN SCALE (u/s): ~0.23 u/s steady drift
constexpr float flowSpeed=.012f;     // fraction of the ribbon per second a particle's anchor may flow, either way
constexpr float windDecay=.04f;      // per step: a navigation gust fades in ~0.4 s
constexpr float iconWind=11.3877f,dpadScaleX=1.f; // PARTICLES_UI.mnu ICON WIND, DPAD SCALE X
constexpr float windPerIconWind=.06f; // u/s of gust per unit ICON WIND (reconstructed)
constexpr float particleGain=2.f;   // _Gamma: particle output gain, so that specular glints cross the HDR glare threshold
                                     // (the plugin draws particles into its x8-encoded frame; that scale is not traced)
constexpr double step=1./60;         // the plugin advances once per frame
}

bool Particles::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    const char* names[2]={"particles_quads","particles_second"};
    for(int i=0;i<2;++i) {
        Pass& pass=passes_[size_t(i)];QString error;
        try {
            pass.vertex=CgProgram::load(assets.qglFile("lines",QString("lib/particles/%1.vpo").arg(names[i])),&error);
            if(pass.vertex.valid())pass.fragment=CgProgram::load(assets.qglFile("lines",QString("lib/particles/%1.fpo").arg(names[i])),&error);
        } catch(const std::exception& e) { error=e.what(); }
        if(!pass.vertex.valid() || !pass.fragment.valid()){qWarning().noquote()<<"particles:"<<names[i]<<"unavailable:"<<error;return false;}
        if(!pass.program.addShaderFromSourceCode(QOpenGLShader::Vertex,pass.vertex.glsl) || !pass.program.addShaderFromSourceCode(QOpenGLShader::Fragment,pass.fragment.glsl) || !pass.program.link()) {
            qWarning().noquote()<<"particles:"<<names[i]<<"link failed:"<<pass.program.log();return false;
        }
    }
    QImage iridescent;
    try { iridescent=Assets::decodeTga(assets.qglFile("lines","particles/proc_iridescent.tga")); } catch(const std::exception& e) { qWarning()<<"particles:"<<e.what(); }
    if(iridescent.isNull()){qWarning()<<"particles: proc_iridescent.tga could not be decoded";return false;}
    iridescent_=std::make_unique<QOpenGLTexture>(iridescent);
    iridescent_->setMinificationFilter(QOpenGLTexture::Linear);iridescent_->setMagnificationFilter(QOpenGLTexture::Linear);iridescent_->setWrapMode(QOpenGLTexture::ClampToEdge);
    // Four vertices per particle: in_0 position + alpha, in_8 quad corner, in_11 rotation quaternion.
    std::vector<GLuint> indices;indices.reserve(size_t(maxParticles)*6);
    for(GLuint p=0;p<GLuint(maxParticles);++p)indices.insert(indices.end(),{p*4,p*4+1,p*4+2,p*4+2,p*4+1,p*4+3});
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);gl.glGenBuffers(1,&ibo_);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);gl.glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(size_t(maxParticles)*4*12*sizeof(float)),nullptr,GL_STREAM_DRAW);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_);gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(indices.size()*sizeof(GLuint)),indices.data(),GL_STATIC_DRAW);
    for(int location:{0,8,11}) {
        gl.glEnableVertexAttribArray(GLuint(location));
        gl.glVertexAttribPointer(GLuint(location),4,GL_FLOAT,GL_FALSE,12*sizeof(float),reinterpret_cast<void*>(size_t(location==0?0:location==8?4:8)*sizeof(float)));
    }
    gl.glBindVertexArray(0);
    particles_.reserve(maxParticles);vertices_.reserve(size_t(maxParticles)*48);
    ready_=true;
    qInfo()<<"particles: particles_quads"<<passes_[0].vertex.instructionCount<<"/"<<passes_[0].fragment.instructionCount
           <<"+ particles_second"<<passes_[1].vertex.instructionCount<<"/"<<passes_[1].fragment.instructionCount<<"instructions, iridescent"<<iridescent.size();
    return true;
}

void Particles::update(double seconds,const std::vector<QVector3D>& emitter) {
    if(!ready_ || emitter.size()<2)return;
    std::uniform_real_distribution<float> unit(0.f,1.f);std::normal_distribution<float> gauss(0.f,1.f);
    // Emit uniformly by arc length: the ribbon's x is quadratic in its parameter, so equal parameter steps bunch up
    // at the left end.
    std::vector<float> length(emitter.size(),0.f);
    for(size_t k=1;k<emitter.size();++k)length[k]=length[k-1]+(emitter[k]-emitter[k-1]).length();
    const auto pointAt=[&](float s) {
        const auto it=std::upper_bound(length.begin(),length.end(),s);
        const size_t k=std::clamp<size_t>(size_t(it-length.begin()),1,emitter.size()-1);
        const float span=std::max(1e-6f,length[k]-length[k-1]);
        return emitter[k-1]+(emitter[k]-emitter[k-1])*std::clamp((s-length[k-1])/span,0.f,1.f);
    };
    const auto stepOnce=[&] {
        // Emission: EMIT PER FRAME tries with EMIT PROB each, from a random point on the ribbon's centre line, inside
        // a cone of EMIT CONE ANGLE around up (down with EMIT NEG PROB), speed MIN + MUL x (1 +- VAR).
        for(int i=0;i<int(std::ceil(emitPerFrame));++i) {
            if(unit(random_)>emitProb*emitRate*std::min(1.f,emitPerFrame-i) || int(particles_.size())>=maxParticles)continue;
            const float cone=emitConeAngle*float(std::numbers::pi)/180.f*std::sqrt(unit(random_)),around=unit(random_)*2.f*float(std::numbers::pi);
            QVector3D direction(std::sin(cone)*std::cos(around),std::cos(cone),std::sin(cone)*std::sin(around));
            if(unit(random_)<emitNegProb)direction.setY(-direction.y());
            Particle p;
            p.anchor=unit(random_);p.flow=(unit(random_)*2.f-1.f)*flowSpeed;
            p.offset=QVector3D(gauss(random_),gauss(random_),gauss(random_))*emitScatter;
            p.velocity=direction*(emitVelMin+emitVelMul*(1.f+emitVelVar*(unit(random_)*2.f-1.f)));
            p.rotation=QQuaternion::fromAxisAndAngle(QVector3D(gauss(random_),gauss(random_),gauss(random_)).normalized(),unit(random_)*360.f);
            p.spinAxis=QVector3D(gauss(random_),gauss(random_),gauss(random_)).normalized();
            p.spin=(unit(random_)*2.f-1.f)*spinTimeScale;
            p.agingRate=agingSpeed*(1.f+agingVariance*(unit(random_)*2.f-1.f));
            particles_.push_back(p);
        }
        // Motion, per 1/60 s step, in firmware units per second: FRICTION per step, a BROWNIAN kick per step that keeps
        // a steady wandering drift (~BROWNIAN / sqrt(2 FRICTION) per axis), GRAVITY, navigation wind. Each particle rides
        // the ribbon at its anchor (which flows slowly along it), so it moves with the wave.
        for(auto& p:particles_) {
            p.velocity*=1.f-friction;
            p.velocity+=QVector3D(gauss(random_),gauss(random_),gauss(random_))*brownianScale*brownianKick;
            p.velocity.setY(p.velocity.y()+gravity);
            p.offset+=(p.velocity+wind_)*float(step);
            p.anchor=std::clamp(p.anchor+p.flow*float(step),0.f,1.f);
            p.position=pointAt(p.anchor*length.back())*sceneScale+p.offset;
            p.age+=p.agingRate;
            p.rotation=(QQuaternion::fromAxisAndAngle(p.spinAxis,p.spin)*p.rotation).normalized();
        }
        wind_*=1.f-windDecay;
        std::erase_if(particles_,[](const Particle& p){return p.age>=1.f;});
    };
    if(simulated_<0 || seconds<simulated_ || seconds-simulated_>2) { // start, clock jump or long pause: warm up
        particles_.clear();simulated_=seconds;
        for(int i=0;i<int(1.2f/agingSpeed);++i)stepOnce();
    }
    for(;simulated_+step<=seconds;simulated_+=step)stepOnce();
}

void Particles::nudge(float direction) {
    // A gust against the navigation direction: the menu slides one way and the dust is pushed the other.
    wind_+=QVector3D(-direction*dpadScaleX*iconWind*windPerIconWind,0,0);
}

void Particles::render(QOpenGLFunctions_3_3_Core& gl,float light,int pixelWidth,int pixelHeight,bool glarePass) {
    if(!ready_ || particles_.empty())return;
    const auto mix=[&](float n,float d){return n+(d-n)*light;};
    if(!glarePass) { // the glare pass reuses this frame's vertices
        vertices_.clear();
        static const float corners[4][2]={{0,0},{1,0},{0,1},{1,1}};
        for(const auto& p:particles_) {
            const float alpha=std::sin(float(std::numbers::pi)*std::clamp(p.age,0.f,1.f)); // fade in and out over the life
            for(const auto& c:corners)vertices_.insert(vertices_.end(),{p.position.x(),p.position.y(),p.position.z(),alpha,c[0],c[1],0,0,
                                                                        p.rotation.x(),p.rotation.y(),p.rotation.z(),p.rotation.scalar()});
        }
    }
    // View space is world space; positions are in firmware units, so the projection undoes the scale.
    QMatrix4x4 projection;projection.perspective(71.846f,float(pixelWidth)/float(pixelHeight),.1f,100.f);projection.scale(1.f/sceneScale);
    // _LightPack, by column: x eye, y spot position / specular power, z attenuation, w lambert / specular / exposure / colour control.
    const QVector4D lightPack[4]={{0,spotX,spotAttn[0],lambertCoeff},{0,spotY,spotAttn[1],specularCoeff},{0,spotZ,spotAttn[2],exposure},{0,specularPower,0,colorControl}};
    const QVector4D nearControl(1.f,nearFuzziness,nearAlign,0.f);
    // _Glare (particles_second): x intensity, y quad scale, z falloff exponent, w falloff rate: e^(-w r^z) around the particle.
    const QVector4D glare(mix(glareNight,glareDay),glareScale,glareP1,glareP2);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    if(!glarePass)gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(vertices_.size()*sizeof(float)),vertices_.data());
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_CULL_FACE);gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    iridescent_->bind(0);
    {
        Pass& pass=passes_[glarePass?1:0];
        std::vector<QVector4D> vc(468);
        for(const auto& param:pass.vertex.parameters)if(param.resourceIndex>=0 && param.resourceIndex<468 && !param.defaults.isEmpty())
            vc[param.resourceIndex]=QVector4D(param.defaults.value(0),param.defaults.value(1),param.defaults.value(2),param.defaults.value(3));
        for(int row=0;row<4;++row){vc[256+row]=projection.row(row);vc[260+row]=QVector4D();vc[260+row][row]=1;vc[264+row]=lightPack[row];}
        const auto setVc=[&](const char* name,const QVector4D& value){const auto it=pass.vertex.parameters.constFind(name);if(it!=pass.vertex.parameters.cend() && it->resourceIndex>=0 && it->resourceIndex<468)vc[it->resourceIndex]=value;};
        setVc("_ParticleSize",QVector4D(mix(sizeMiddleNight,sizeMiddleDay),sizeNear,sizeFar,0));
        setVc("_Transparency",QVector4D(fresnel,globalAlpha*globalAlpha_,0,0));
        setVc("_Focus",QVector4D(nearFocusDist,nearFocus,farFocus-mix(farFocusDistNight,farFocusDistDay),farFocus));
        setVc("_FocusCurves",QVector4D(nearFocusPow,farFocusPow,darknessAngle,0)); // SIZE ALIGN's role is not traced
        setVc("_FrontFacingQuaternion",QVector4D(0,0,0,1));
        setVc("_NearControl",nearControl);setVc("_Glare",glare);
        setVc("_LifeBoundsMin",QVector4D(-30,-14,-40,0));setVc("_LifeBoundsMax",QVector4D(30,14,-1,0));
        setVc("_Darkness",QVector4D(nearDarkness,farDarkness,0,0));
        QVector<QVector4D> fc=pass.fragment.constants;
        const auto setFc=[&](const char* name,const QVector4D& value){for(int slot:pass.fragment.parameters.value(name).constantSlots)if(slot<fc.size())fc[slot]=value;};
        for(int row=0;row<4;++row)setFc(QString("_LightPack[%1]").arg(row).toUtf8().constData(),lightPack[row]);
        setFc("_NearControl",nearControl);setFc("_Glare",glare);setFc("_Color",QVector4D(1,1,1,0));setFc("_Gamma",QVector4D(1,1,1,1)*particleGain);
        setFc("_IridescentExp",QVector4D(1,1,1,1)*iridescentExp);
        pass.program.bind();
        pass.program.setUniformValueArray("vc",vc.data(),int(vc.size()));pass.program.setUniformValueArray("fc",fc.constData(),fc.size());
        pass.program.setUniformValue("tex0",0);
        gl.glDrawElements(GL_TRIANGLES,GLsizei(particles_.size()*6),GL_UNSIGNED_INT,nullptr);
        pass.program.release();
    }
    gl.glBindVertexArray(0);iridescent_->release(0);
    gl.glDisable(GL_BLEND);
}
}
