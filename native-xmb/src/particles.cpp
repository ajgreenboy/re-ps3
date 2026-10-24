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
constexpr float nearAlign=.722f,sizeAlign=13.19f;
// Reconstructed (the SPU simulation and the CPU uniform setters are not traced):
constexpr float sceneScale=3.2f;     // firmware units per view unit: puts the ribbon (view z = -3) near the focus band
constexpr int maxParticles=1500;
constexpr float emitScatter=.6f;     // firmware units around the emission point (particles fill a volume, not the line)
constexpr float darknessAngle=4.f;   // _FocusCurves.z: divides the angular-size term of near/far darkness
constexpr double step=1./60;         // the plugin advances once per frame
}

bool Particles::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    QString error;
    try {
        vertexCg_=CgProgram::load(assets.qglFile("lines","lib/particles/particles_quads.vpo"),&error);
        if(vertexCg_.valid())fragmentCg_=CgProgram::load(assets.qglFile("lines","lib/particles/particles_quads.fpo"),&error);
    } catch(const std::exception& e) { error=e.what(); }
    if(!vertexCg_.valid() || !fragmentCg_.valid()){qWarning().noquote()<<"particles: programs unavailable:"<<error;return false;}
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexCg_.glsl) || !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentCg_.glsl) || !program_.link()) {
        qWarning().noquote()<<"particles: link failed:"<<program_.log();return false;
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
    qInfo()<<"particles: particles_quads"<<vertexCg_.instructionCount<<"/"<<fragmentCg_.instructionCount<<"instructions, iridescent"<<iridescent.size();
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
            if(unit(random_)>emitProb*std::min(1.f,emitPerFrame-i) || int(particles_.size())>=maxParticles)continue;
            const QVector3D origin=pointAt(unit(random_)*length.back())*sceneScale;
            const float cone=emitConeAngle*float(std::numbers::pi)/180.f*std::sqrt(unit(random_)),around=unit(random_)*2.f*float(std::numbers::pi);
            QVector3D direction(std::sin(cone)*std::cos(around),std::cos(cone),std::sin(cone)*std::sin(around));
            if(unit(random_)<emitNegProb)direction.setY(-direction.y());
            Particle p;
            p.position=origin+QVector3D(gauss(random_),gauss(random_),gauss(random_))*emitScatter;
            p.velocity=direction*(emitVelMin+emitVelMul*(1.f+emitVelVar*(unit(random_)*2.f-1.f)));
            p.rotation=QQuaternion::fromAxisAndAngle(QVector3D(gauss(random_),gauss(random_),gauss(random_)).normalized(),unit(random_)*360.f);
            p.spinAxis=QVector3D(gauss(random_),gauss(random_),gauss(random_)).normalized();
            p.spin=(unit(random_)*2.f-1.f)*spinTimeScale;
            p.agingRate=agingSpeed*(1.f+agingVariance*(unit(random_)*2.f-1.f));
            particles_.push_back(p);
        }
        // Motion: friction, brownian kick, gravity; aging; spin.
        for(auto& p:particles_) {
            p.velocity*=1.f-friction;
            p.velocity+=QVector3D(gauss(random_),gauss(random_),gauss(random_))*brownianScale*deltaTime*4.f;
            p.velocity.setY(p.velocity.y()+gravity);
            p.position+=p.velocity*deltaTime;
            p.age+=p.agingRate;
            p.rotation=(QQuaternion::fromAxisAndAngle(p.spinAxis,p.spin)*p.rotation).normalized();
        }
        std::erase_if(particles_,[](const Particle& p){return p.age>=1.f;});
    };
    if(simulated_<0 || seconds<simulated_ || seconds-simulated_>2) { // start, clock jump or long pause: warm up
        particles_.clear();simulated_=seconds;
        for(int i=0;i<int(1.2f/agingSpeed);++i)stepOnce();
    }
    for(;simulated_+step<=seconds;simulated_+=step)stepOnce();
}

void Particles::render(QOpenGLFunctions_3_3_Core& gl,float light,int pixelWidth,int pixelHeight) {
    if(!ready_ || particles_.empty())return;
    const auto mix=[&](float n,float d){return n+(d-n)*light;};
    vertices_.clear();
    static const float corners[4][2]={{0,0},{1,0},{0,1},{1,1}};
    for(const auto& p:particles_) {
        const float alpha=std::sin(float(std::numbers::pi)*std::clamp(p.age,0.f,1.f)); // fade in and out over the life
        for(const auto& c:corners)vertices_.insert(vertices_.end(),{p.position.x(),p.position.y(),p.position.z(),alpha,c[0],c[1],0,0,
                                                                    p.rotation.x(),p.rotation.y(),p.rotation.z(),p.rotation.scalar()});
    }
    // View space is world space; positions are in firmware units, so the projection undoes the scale.
    QMatrix4x4 projection;projection.perspective(71.846f,float(pixelWidth)/float(pixelHeight),.1f,100.f);projection.scale(1.f/sceneScale);
    std::vector<QVector4D> vc(468);
    for(const auto& param:vertexCg_.parameters)if(param.resourceIndex>=0 && param.resourceIndex<468 && !param.defaults.isEmpty())
        vc[param.resourceIndex]=QVector4D(param.defaults.value(0),param.defaults.value(1),param.defaults.value(2),param.defaults.value(3));
    for(int row=0;row<4;++row){vc[256+row]=projection.row(row);vc[260+row][row]=1;}
    // _LightPack, by column: x eye, y spot position / specular power, z attenuation, w lambert / specular / exposure / colour control.
    const QVector4D lightPack[4]={{0,spotX,spotAttn[0],lambertCoeff},{0,spotY,spotAttn[1],specularCoeff},{0,spotZ,spotAttn[2],exposure},{0,specularPower,0,colorControl}};
    for(int row=0;row<4;++row)vc[264+row]=lightPack[row];
    const QVector4D nearControl(1.f,nearFuzziness,nearAlign,0.f);
    const auto setVc=[&](const char* name,const QVector4D& value){const auto it=vertexCg_.parameters.constFind(name);if(it!=vertexCg_.parameters.cend() && it->resourceIndex>=0)vc[it->resourceIndex]=value;};
    setVc("_ParticleSize",QVector4D(mix(sizeMiddleNight,sizeMiddleDay),sizeNear,sizeFar,0));
    setVc("_Transparency",QVector4D(fresnel,globalAlpha,0,0));
    setVc("_Focus",QVector4D(nearFocusDist,nearFocus,farFocus-mix(farFocusDistNight,farFocusDistDay),farFocus));
    setVc("_FocusCurves",QVector4D(nearFocusPow,farFocusPow,darknessAngle,0)); // SIZE ALIGN's role is not traced
    setVc("_FrontFacingQuaternion",QVector4D(0,0,0,1));
    setVc("_NearControl",nearControl);
    setVc("_LifeBoundsMin",QVector4D(-30,-14,-40,0));setVc("_LifeBoundsMax",QVector4D(30,14,-1,0));
    setVc("_Darkness",QVector4D(nearDarkness,farDarkness,0,0));
    QVector<QVector4D> fc=fragmentCg_.constants;
    const auto setFc=[&](const char* name,const QVector4D& value){for(int slot:fragmentCg_.parameters.value(name).constantSlots)if(slot<fc.size())fc[slot]=value;};
    for(int row=0;row<4;++row)setFc(QString("_LightPack[%1]").arg(row).toUtf8().constData(),lightPack[row]);
    setFc("_NearControl",nearControl);setFc("_Color",QVector4D(1,1,1,0));setFc("_Gamma",QVector4D(1,1,1,1));
    setFc("_IridescentExp",QVector4D(1,1,1,1)*iridescentExp);
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_CULL_FACE);gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    program_.bind();
    program_.setUniformValueArray("vc",vc.data(),int(vc.size()));program_.setUniformValueArray("fc",fc.constData(),fc.size());
    program_.setUniformValue("tex0",0);iridescent_->bind(0);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(vertices_.size()*sizeof(float)),vertices_.data());
    gl.glDrawElements(GL_TRIANGLES,GLsizei(particles_.size()*6),GL_UNSIGNED_INT,nullptr);
    gl.glBindVertexArray(0);iridescent_->release(0);program_.release();
    gl.glDisable(GL_BLEND);
}
}
