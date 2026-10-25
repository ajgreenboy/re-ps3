#include "hdr.h"
#include <cmath>
#include <QDebug>

namespace xmb {
namespace {
// HDR.mnu: override/night and override/day (qgl/lines.qrc), blended by NightDayBlend like the other presets.
struct Preset { float exposure,white,glareLevel,threshold,radius[3],sumPow; };
constexpr Preset night{1.41f,.999878f,1.33f,.699812f,{1.1f,1.36f,1.5f},.5f};
constexpr Preset day{1.05f,.899181f,1.10245f,.738857f,{1.23888f,1.43176f,1.55787f},.557478f};
// override/coldboot1 (the boot's first preset; coldboot2 has day's values).
constexpr Preset coldboot1{1.64f,1.f,1.74f,.2f,{1.23888f,1.43176f,1.55787f},.557478f};
constexpr int texSize=8,texMaxMip=8; // TEX SIZE (glare source 2^8 wide), TEX MAX MIP
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
std::unique_ptr<QOpenGLFramebufferObject> floatTarget(int width,int height,bool mipmap=false) {
    QOpenGLFramebufferObjectFormat format;format.setInternalTextureFormat(GL_RGBA16F);format.setMipmap(mipmap);
    return std::make_unique<QOpenGLFramebufferObject>(std::max(1,width),std::max(1,height),format);
}
}

bool Hdr::load(Assets& assets,Pass& pass,const char* fragment,const char* vertexName) {
    QString error;CgProgram vertexCg;
    try {
        pass.cg=CgProgram::load(assets.qglFile("lines",QString("lib/glutils/")+fragment),&error);
        if(vertexName && pass.cg.valid())vertexCg=CgProgram::load(assets.qglFile("lines",QString("lib/glutils/")+vertexName),&error);
    } catch(const std::exception& e) { error=e.what(); }
    if(!pass.cg.valid() || (vertexName && !vertexCg.valid())){qWarning().noquote()<<"hdr:"<<fragment<<"unavailable:"<<error;return false;}
    if(!pass.program.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexName?vertexCg.glsl:QString(fullscreenVertex)) ||
       !pass.program.addShaderFromSourceCode(QOpenGLShader::Fragment,pass.cg.glsl) || !pass.program.link()) {
        qWarning().noquote()<<"hdr:"<<fragment<<"failed:"<<pass.program.log();return false;
    }
    return true;
}
void Hdr::setFc(Pass& pass,QVector<QVector4D>& fc,const char* name,const QVector4D& value) {
    for(int slot:pass.cg.parameters.value(name).constantSlots)if(slot<fc.size())fc[slot]=value;
}

bool Hdr::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    if(!load(assets,toneMap_,"ToneMap.fpo") || !load(assets,glareSource_,"GlareSourceNoTone.fpo") || !load(assets,gaussian_,"Gaussian.fpo","Gaussian.vpo") ||
       !load(assets,accGlare_,"AccGlare.fpo") || !load(assets,applyGlare_,"ApplyGlare.fpo"))return false;
    gl.glGenVertexArrays(1,&emptyVao_);
    // Gaussian.vpo reads POSITION (location 0) and TEXCOORD0 (location 8): one fullscreen triangle.
    const float triangle[]={-1,-1,0,1, 0,0,0,0,  3,-1,0,1, 2,0,0,0,  -1,3,0,1, 0,2,0,0};
    gl.glGenVertexArrays(1,&gaussianVao_);gl.glGenBuffers(1,&gaussianVbo_);
    gl.glBindVertexArray(gaussianVao_);gl.glBindBuffer(GL_ARRAY_BUFFER,gaussianVbo_);
    gl.glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);
    gl.glEnableVertexAttribArray(8);gl.glVertexAttribPointer(8,4,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(4*sizeof(float)));
    gl.glBindVertexArray(0);
    ready_=true;
    qInfo()<<"hdr: ToneMap, GlareSourceNoTone, Gaussian, AccGlare, ApplyGlare translated";
    return true;
}

GLuint Hdr::begin(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight) {
    if(!scene_ || width_!=pixelWidth || height_!=pixelHeight) {
        width_=pixelWidth;height_=pixelHeight;
        scene_=floatTarget(width_,height_);toned_=floatTarget(width_,height_);
        const int sw=1<<texSize,sh=std::max(1,int(std::lround(double(sw)*height_/width_)));
        source_=floatTarget(sw,sh,true);accumulated_=floatTarget(sw,sh);
        blurX_.clear();blurY_.clear();
        for(int level=0;level<texMaxMip && (sw>>level)>=1 && (sh>>level)>=1;++level) {
            blurX_.push_back(floatTarget(sw>>level,sh>>level));blurY_.push_back(floatTarget(sw>>level,sh>>level));
        }
        for(auto* target:{source_.get(),accumulated_.get()}) {
            gl.glBindTexture(GL_TEXTURE_2D,target->texture());
            gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        }
        for(const auto& list:{&blurX_,&blurY_})for(const auto& target:*list) {
            gl.glBindTexture(GL_TEXTURE_2D,target->texture());
            gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        }
        gl.glBindTexture(GL_TEXTURE_2D,0);
    }
    scene_->bind();gl.glViewport(0,0,width_,height_);gl.glClearColor(0,0,0,1);gl.glClear(GL_COLOR_BUFFER_BIT);
    return scene_->handle();
}

void Hdr::finish(QOpenGLFunctions_3_3_Core& gl,float nightDayBlend,GLuint targetFbo,const std::function<void(int,int)>& extraGlare) {
    // The clock's night/day blend, reached from the boot presets (coldboot1 -> coldboot2 = day -> clock).
    const auto mix=[&](float boot1,float n,float d){
        const float boot=boot1+(d-boot1)*toColdboot2_;
        return boot+(n+(d-n)*nightDayBlend-boot)*toClock_;
    };
    const float exposure=mix(coldboot1.exposure,night.exposure,day.exposure),white=mix(coldboot1.white,night.white,day.white);
    const float glareLevel=mix(coldboot1.glareLevel,night.glareLevel,day.glareLevel),threshold=mix(coldboot1.threshold,night.threshold,day.threshold);
    const float sumPow=mix(coldboot1.sumPow,night.sumPow,day.sumPow);
    const float radius[3]={mix(coldboot1.radius[0],night.radius[0],day.radius[0]),mix(coldboot1.radius[1],night.radius[1],day.radius[1]),
                           mix(coldboot1.radius[2],night.radius[2],day.radius[2])};
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_SCISSOR_TEST);gl.glDisable(GL_BLEND);
    const auto fullscreen=[&](QOpenGLShaderProgram& program,const QVector<QVector4D>& fc) {
        program.setUniformValueArray("fc",fc.constData(),fc.size());
        gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);gl.glBindVertexArray(0);
    };
    const auto bindTexture=[&](int unit,GLuint texture){gl.glActiveTexture(GLenum(GL_TEXTURE0+unit));gl.glBindTexture(GL_TEXTURE_2D,texture);};
    // 1. ToneMap: the scene at EXPOSURE through the white-level curve.
    {
        QVector<QVector4D> fc=toneMap_.cg.constants;
        setFc(toneMap_,fc,"_Exposure",QVector4D(1,1,1,1)*exposure);setFc(toneMap_,fc,"_WhiteSqrRcp",QVector4D(1,1,1,1)/(white*white));
        toned_->bind();gl.glViewport(0,0,width_,height_);
        toneMap_.program.bind();toneMap_.program.setUniformValue("tex0",0);bindTexture(0,scene_->texture());
        fullscreen(toneMap_.program,fc);toneMap_.program.release();
    }
    // 2. Glare source: exposed colour above GLARE THRESH (threshold given in scene units), then its mip chain.
    {
        QVector<QVector4D> fc=glareSource_.cg.constants;
        setFc(glareSource_,fc,"_Threshold",QVector4D(1,1,1,1)*(threshold/exposure));
        source_->bind();gl.glViewport(0,0,source_->width(),source_->height());
        glareSource_.program.bind();glareSource_.program.setUniformValue("tex0",0);bindTexture(0,scene_->texture());
        fullscreen(glareSource_.program,fc);glareSource_.program.release();
        if(extraGlare) {
            source_->bind();gl.glViewport(0,0,source_->width(),source_->height());
            extraGlare(source_->width(),source_->height());
            gl.glDisable(GL_BLEND);
        }
        bindTexture(0,source_->texture());gl.glGenerateMipmap(GL_TEXTURE_2D);
    }
    // 3. Per level: 15-tap Gaussian across then down, each channel with its own radius (in texels of that level),
    //    then AccGlare adds the level with weight SUM POW^level (normalised): narrow levels dominate, wide ones haze.
    QVector<QVector4D> weights(8);
    for(int c=0;c<3;++c) {
        float sum=0;for(int k=-7;k<=7;++k)sum+=std::exp(-float(k*k)/(2*radius[c]*radius[c]));
        for(int k=0;k<8;++k)weights[k][c]=std::exp(-float(k*k)/(2*radius[c]*radius[c]))/sum;
    }
    float levelSum=0;for(size_t level=0;level<blurX_.size();++level)levelSum+=std::pow(sumPow,float(level));
    accumulated_->bind();gl.glViewport(0,0,accumulated_->width(),accumulated_->height());gl.glClearColor(0,0,0,0);gl.glClear(GL_COLOR_BUFFER_BIT);
    QVector<QVector4D> gaussianFc=gaussian_.cg.constants;
    for(int k=0;k<8;++k)setFc(gaussian_,gaussianFc,QString("_Weights%1").arg(k).toUtf8().constData(),weights[k]);
    std::vector<QVector4D> vc(468);
    for(int row=0;row<4;++row)vc[256+row][row]=1; // _ModelviewProjection = identity
    for(size_t level=0;level<blurX_.size();++level) {
        const int w=blurX_[level]->width(),h=blurX_[level]->height();
        gaussian_.program.bind();gaussian_.program.setUniformValue("tex0",0);
        gaussian_.program.setUniformValueArray("fc",gaussianFc.constData(),gaussianFc.size());
        for(int pass=0;pass<2;++pass) {
            vc[467]=pass==0?QVector4D(1.f/w,0,0,0):QVector4D(0,1.f/h,0,0);
            gaussian_.program.setUniformValueArray("vc",vc.data(),int(vc.size()));
            if(pass==0) { // read this level of the source
                bindTexture(0,source_->texture());
                gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,GLint(level));gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(level));
                blurX_[level]->bind();
            } else { bindTexture(0,blurX_[level]->texture());blurY_[level]->bind(); }
            gl.glViewport(0,0,w,h);
            gl.glBindVertexArray(gaussianVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);gl.glBindVertexArray(0);
        }
        gaussian_.program.release();
        QVector<QVector4D> fc=accGlare_.cg.constants;
        setFc(accGlare_,fc,"_Weight",QVector4D(1,1,1,1)*(std::pow(sumPow,float(level))/levelSum*exposure));
        accumulated_->bind();gl.glViewport(0,0,accumulated_->width(),accumulated_->height());
        gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
        accGlare_.program.bind();accGlare_.program.setUniformValue("tex0",0);bindTexture(0,blurY_[level]->texture());
        fullscreen(accGlare_.program,fc);accGlare_.program.release();
        gl.glDisable(GL_BLEND);
    }
    bindTexture(0,source_->texture());
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,1000);
    // 4. ApplyGlare: tone-mapped screen x 1 + glare x GLARE LEVEL, into the caller's target.
    {
        QVector<QVector4D> fc=applyGlare_.cg.constants;
        setFc(applyGlare_,fc,"_Weight1",QVector4D(1,glareLevel,0,0));
        gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,width_,height_);
        applyGlare_.program.bind();applyGlare_.program.setUniformValue("tex0",0);applyGlare_.program.setUniformValue("tex1",1);
        bindTexture(0,toned_->texture());bindTexture(1,accumulated_->texture());
        fullscreen(applyGlare_.program,fc);applyGlare_.program.release();
        bindTexture(1,0);bindTexture(0,0);
    }
}
}
