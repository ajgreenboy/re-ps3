#include "ribbon.h"
#include <cmath>
#include <QDebug>
#include <QImage>
#include <QMatrix4x4>
#include <QVector3D>

namespace xmb {
namespace {
// LINE1.mnu, override/day and override/night (qgl/lines.qrc). The ribbon's look comes from these.
constexpr float thinness=0.f;       // shader value; the presets say THINNESS 1, which the plugin remaps (not traced): 0 = full stripe period
constexpr float spacingDay=407.658f,spacingNight=408.298f;
constexpr float brightnessDay=.701917f,brightnessNight=.9f;
constexpr float fresnelDay=.638971f,fresnelNight=.52f;
constexpr float mipmapBiasDay=1.86707f,mipmapBiasNight=1.75802f;
constexpr float fovy=71.846f;                                        // BACKGROUND.mnu
constexpr QVector3D colour3{.847347f,.846155f,.846448f},colour4{.92487f,.922603f,.922933f}; // BACKGROUND.mnu 3/4: line gradient
constexpr float ffdScaleX=5.67726f;                                   // FFD SCALE1 X: field x (≈ -0.15..1.35) to lattice width
// Fitted to the reference video (audit/VIDEO-REFERENCES.md, ribbon motion): the SPU lattice and camera are not ported.
constexpr float timeScale=.12f;      // FFD _Time per second: the travelling wave sin(7.85a - 2.5t) brings a swell every ~21 s
constexpr float latticeDepth=3.f;    // lattice plane distance (the plugin seeds its control grid at z = -3)
constexpr float restY=.30f;          // view-space height of the field's zero line
constexpr float fieldToY=-.60f;      // view-space height per field unit (negative: the swell pushes the crest down)
constexpr float sheetRise=.22f,sheetDepth=1.1f; // the line sheet's extent across its width (height, depth)
constexpr float normalScale=1.5f;    // line weight: lines1.vpo scales by |n| / |position|

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
    if(!lines_.addShaderFromSourceCode(QOpenGLShader::Vertex,linesVertexCg_.glsl) || !lines_.addShaderFromSourceCode(QOpenGLShader::Fragment,linesFragmentCg_.glsl) || !lines_.link()) {
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
    // _UVW: along the ribbon in .x, across it in .z, sampled at texel centres.
    std::vector<float> uvw(size_t(along_)*across_*4);
    for(int j=0;j<across_;++j)for(int i=0;i<along_;++i) {
        float* p=&uvw[(size_t(j)*along_+i)*4];p[0]=float(i)/(along_-1);p[1]=0;p[2]=float(j)/(across_-1);p[3]=1;
    }
    uvw_=floatTexture(along_,across_,uvw,QOpenGLTexture::Nearest);
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
    stripes_->setWrapMode(QOpenGLTexture::ClampToEdge);stripes_->setLevelofDetailBias(mipmapBiasDay);
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
    QOpenGLFramebufferObjectFormat fieldFormat;fieldFormat.setInternalTextureFormat(GL_RGBA32F);
    field_=std::make_unique<QOpenGLFramebufferObject>(along_,across_,fieldFormat);
    // Sheet: along_ x across_ vertices, attributes in_0 (position), in_8 (stripe coordinate, edge fade), in_9 (normal).
    std::vector<GLuint> indices;
    for(int j=0;j+1<across_;++j)for(int i=0;i+1<along_;++i) {
        const GLuint a=GLuint(j*along_+i),b=a+1,c=a+along_,d=c+1;
        indices.insert(indices.end(),{a,b,c,b,d,c});
    }
    indexCount_=GLsizei(indices.size());
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);gl.glGenBuffers(1,&ibo_);gl.glGenVertexArrays(1,&emptyVao_);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);gl.glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(along_*across_*12*sizeof(float)),nullptr,GL_STREAM_DRAW);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_);gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(indices.size()*sizeof(GLuint)),indices.data(),GL_STATIC_DRAW);
    for(int location:{0,8,9}) {
        gl.glEnableVertexAttribArray(GLuint(location));
        gl.glVertexAttribPointer(GLuint(location),4,GL_FLOAT,GL_FALSE,12*sizeof(float),reinterpret_cast<void*>(size_t(location==0?0:location==8?4:8)*sizeof(float)));
    }
    gl.glBindVertexArray(0);
    fieldPixels_.resize(size_t(along_)*across_*4);vertices_.resize(size_t(along_)*across_*12);
    ready_=true;
    qInfo()<<"ribbon: ffd_shader1"<<ffdCg_.instructionCount<<"+ lines1"<<linesVertexCg_.instructionCount<<"/"<<linesFragmentCg_.instructionCount<<"instructions, FresLUT"<<lut.size();
    return true;
}

void Ribbon::render(QOpenGLFunctions_3_3_Core& gl,double seconds,float light,int pixelWidth,int pixelHeight,GLuint targetFbo) {
    if(!ready_ || pixelWidth<=0 || pixelHeight<=0)return;
    const auto mix=[&](float night,float day){return night+(day-night)*light;};
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_SCISSOR_TEST);gl.glDisable(GL_CULL_FACE);
    // 1. The wave field (firmware ffd_shader1) for every (along, across) sample, read back like the plugin does.
    QVector<QVector4D> fc=ffdCg_.constants;
    for(int slot:ffdCg_.parameters.value("_Time").constantSlots)if(slot<fc.size())fc[slot]=QVector4D(1,1,1,1)*float(seconds*timeScale);
    field_->bind();gl.glViewport(0,0,along_,across_);gl.glDisable(GL_BLEND);
    ffd_.bind();ffd_.setUniformValueArray("fc",fc.constData(),fc.size());ffd_.setUniformValue("tex0",0);uvw_->bind(0);
    gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);
    gl.glReadPixels(0,0,along_,across_,GL_RGBA,GL_FLOAT,fieldPixels_.data());
    uvw_->release(0);ffd_.release();
    // 2. The sheet in view space: x from the field's x across the lattice width, y from the field's height,
    //    the across coordinate rising and receding so that the ~400 lines fan out in perspective.
    const float aspect=float(pixelWidth)/float(pixelHeight);
    QMatrix4x4 projection;projection.perspective(fovy,aspect,.1f,100.f);
    std::vector<QVector3D> positions(size_t(along_)*across_);
    for(int j=0;j<across_;++j)for(int i=0;i<along_;++i) {
        const float* f=&fieldPixels_[(size_t(j)*along_+i)*4];
        const float c=float(j)/(across_-1)-.5f;
        positions[size_t(j)*along_+i]=QVector3D((f[0]-.6f)*ffdScaleX,restY+fieldToY*f[1]+c*sheetRise,-latticeDepth-c*sheetDepth);
    }
    centreLine_.assign(positions.begin()+ptrdiff_t(across_/2)*along_,positions.begin()+ptrdiff_t(across_/2+1)*along_);
    for(int j=0;j<across_;++j)for(int i=0;i<along_;++i) {
        const auto at=[&](int x,int y){return positions[size_t(std::clamp(y,0,across_-1))*along_+std::clamp(x,0,along_-1)];};
        const QVector3D p=at(i,j),normal=QVector3D::crossProduct(at(i+1,j)-at(i-1,j),at(i,j+1)-at(i,j-1)).normalized()*normalScale;
        const QVector4D clip=projection*QVector4D(p,1);
        const float a=float(i)/(along_-1),c=float(j)/(across_-1);
        // Edge fade (FUN_0004d190): min(1, (.5 - |.5 - u|) * 10) along and across, multiplied.
        const float fade=std::min(1.f,(.5f-std::abs(.5f-a))*10.f)*std::min(1.f,(.5f-std::abs(.5f-c))*10.f);
        float* v=&vertices_[(size_t(j)*along_+i)*12];
        v[0]=clip.x();v[1]=clip.y();v[2]=clip.z();v[3]=clip.w();
        v[4]=c;v[5]=fade;v[6]=0;v[7]=0;
        v[8]=normal.x();v[9]=normal.y();v[10]=normal.z();v[11]=0;
    }
    for(int k=0;k<int(crest_.size());++k) { // centre line of the sheet, screen x -> fraction from the top
        const float sx=float(k)/(crest_.size()-1);float best=-1,bestDistance=1e9f;
        for(int i=0;i<along_;++i) {
            const float* v=&vertices_[(size_t(across_/2)*along_+i)*12];
            const float x=(v[0]/v[3]+1)*.5f,distance=std::abs(x-sx);
            if(distance<bestDistance){bestDistance=distance;best=(1-v[1]/v[3])*.5f;}
        }
        crest_[k]=best;
    }
    // 3. lines1 into a float sum, additively (the firmware sums RGBA8 through _Encode).
    if(!sum_ || sum_->size()!=QSize(pixelWidth,pixelHeight)) {
        QOpenGLFramebufferObjectFormat format;format.setInternalTextureFormat(GL_RGBA16F);
        sum_=std::make_unique<QOpenGLFramebufferObject>(pixelWidth,pixelHeight,format);
    }
    sum_->bind();gl.glViewport(0,0,pixelWidth,pixelHeight);gl.glClearColor(0,0,0,0);gl.glClear(GL_COLOR_BUFFER_BIT);
    gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
    std::vector<QVector4D> vc(468);
    for(const auto& p:linesVertexCg_.parameters)if(p.resourceIndex>=0 && p.resourceIndex<468 && !p.defaults.isEmpty())
        vc[p.resourceIndex]=QVector4D(p.defaults.value(0),p.defaults.value(1),p.defaults.value(2),p.defaults.value(3));
    const auto setVc=[&](const char* name,float value){const auto it=linesVertexCg_.parameters.constFind(name);if(it!=linesVertexCg_.parameters.cend() && it->resourceIndex>=0 && it->resourceIndex<468)vc[it->resourceIndex]=QVector4D(value,value,value,value);};
    setVc("_Fresnel",mix(fresnelNight,fresnelDay));setVc("_Brightness",mix(brightnessNight,brightnessDay));setVc("_MipmapBias",mix(mipmapBiasNight,mipmapBiasDay));
    QVector<QVector4D> lfc=linesFragmentCg_.constants;
    const auto setFc=[&](const char* name,float value){for(int slot:linesFragmentCg_.parameters.value(name).constantSlots)if(slot<lfc.size())lfc[slot]=QVector4D(value,value,value,value);};
    setFc("_Spacing",mix(spacingNight,spacingDay));setFc("_Thinness",thinness);
    lines_.bind();
    lines_.setUniformValueArray("vc",vc.data(),int(vc.size()));lines_.setUniformValueArray("fc",lfc.constData(),lfc.size());
    lines_.setUniformValue("tex0",0);lines_.setUniformValue("tex1",1);lines_.setUniformValue("tex2",2);
    stripes_->bind(0);fresLut_->bind(1);encode_->bind(2);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(vertices_.size()*sizeof(float)),vertices_.data());
    gl.glDrawElements(GL_TRIANGLES,indexCount_,GL_UNSIGNED_INT,nullptr);
    gl.glBindVertexArray(0);
    encode_->release(2);fresLut_->release(1);stripes_->release(0);lines_.release();
    // 4. Composite onto the target (LinesController: background + lines x gradient).
    gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);gl.glViewport(0,0,pixelWidth,pixelHeight);
    composite_.bind();composite_.setUniformValue("sum",0);composite_.setUniformValue("colour3",colour3);composite_.setUniformValue("colour4",colour4);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,sum_->texture());
    gl.glBindVertexArray(emptyVao_);gl.glDrawArrays(GL_TRIANGLES,0,3);gl.glBindVertexArray(0);
    gl.glBindTexture(GL_TEXTURE_2D,0);composite_.release();
    gl.glDisable(GL_BLEND);
}
}
