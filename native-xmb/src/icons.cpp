#include "icons.h"
#include <QDebug>
#include <QFile>
#include <QtEndian>
#include <QVector4D>
#include <cmath>
#include <QtMath>
#include <vector>

namespace xmb {
namespace {
QHash<QString,float> readMnu(const QByteArray& text) {
    QHash<QString,float> values;
    for(const auto& raw:text.split('\n')) {
        const auto parts=QString::fromLatin1(raw).trimmed().split(':');
        if(parts.size()==3 && (parts[1]=="float" || parts[1]=="int"))values.insert(parts[0],parts[2].toFloat());
    }
    return values;
}
constexpr double kDistance=2700,kScaleX=2.8125,kScaleY=5; // steady-state camera of the capture
}

GLuint IconRenderer::texture(QOpenGLFunctions_3_3_Core& gl,const QImage& image,bool mipmaps) {
    // Rows go up in memory order, so t = 0 is the first row as on the RSX.
    const QImage rgba=image.convertToFormat(QImage::Format_RGBA8888);
    GLuint id=0;gl.glGenTextures(1,&id);gl.glBindTexture(GL_TEXTURE_2D,id);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rgba.width(),rgba.height(),0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.constBits());
    if(mipmaps)gl.glGenerateMipmap(GL_TEXTURE_2D);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mipmaps?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    gl.glBindTexture(GL_TEXTURE_2D,0);
    return id;
}

GLuint IconRenderer::normalMap(QOpenGLFunctions_3_3_Core& gl,const QString& name) {
    if(const auto it=normalMaps_.constFind(name);it!=normalMaps_.cend())return it.value();
    // Assets flips icontex for QPainter; flip back to the firmware's memory order.
    const QImage& image=assets_->image("qgl/icontex",name);
    const GLuint id=image.isNull()?0:texture(gl,image.flipped(Qt::Vertical),true);
    normalMaps_.insert(name,id);
    return id;
}

float IconRenderer::value(const QString& key,float light) const {
    const float night=night_.value(key),day=day_.value(key,night);
    return night+(day-night)*light;
}

bool IconRenderer::init(QOpenGLFunctions_3_3_Core& gl,Assets& assets) {
    assets_=&assets;
    QString error;
    try {
        vertexCg_=CgProgram::load(assets.qglFile("icons","lib/icons/quad.vpo"),&error);
        if(vertexCg_.valid())fragmentCg_=CgProgram::load(assets.qglFile("icons","lib/icons/icons_no_ss_no_shad.fpo"),&error);
        day_=readMnu(assets.qglFile("icons","override/day/ICONS.mnu"));
        night_=readMnu(assets.qglFile("icons","override/night/ICONS.mnu"));
    } catch(const std::exception& e) { error=e.what(); }
    if(!vertexCg_.valid() || !fragmentCg_.valid() || night_.isEmpty()){qWarning().noquote()<<"icons: firmware programs unavailable:"<<error;return false;}
    // The program's alpha (envmap alpha x _Ambiant.w: 0.698 for unfocused icons) blends the icon over the frame; the
    // layout's fade (page depth, dialogs) scales it.
    QString fragment=fragmentCg_.glsl;
    fragment.replace("uniform vec4 fc[","uniform float layoutOpacity_;\nuniform vec4 fc[");
    fragment.replace("fragColor = h[0];","fragColor = vec4(h[0].rgb, h[0].a * layoutOpacity_);");
    // Diagnosis: XMB_ICON_DEBUG=0..3 shows that texture unit at the icon's coordinates instead of the result
    // (0-2 at uv, 3 the background cut at v_tc[1]); 4 shows the shaded colour before the final exposure curve.
    if(qEnvironmentVariableIsSet("XMB_ICON_DEBUG")) {
        const int unit=qEnvironmentVariableIntValue("XMB_ICON_DEBUG");
        const QString debug=unit<3?QString("fragColor = vec4(texture(tex%1, v_tc[0].xy).rgb, 1.);").arg(unit):
            unit==3?QString("fragColor = vec4(texture(tex3, v_tc[1].xy).rgb, 1.);"):QString("fragColor = vec4(h[0].rgb * 0.25, 1.);");
        fragment.replace("fragColor = h[0];",debug);
    }
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexCg_.glsl) || !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !program_.link()) {
        qWarning().noquote()<<"icons: link failed:"<<program_.log();return false;
    }
    const QImage& diffuse=assets.image("qgl/icontex","dif");
    const QImage& environment=assets.image("qgl/icons","widgets/other/texenv.dds");
    if(diffuse.isNull() || environment.isNull()){qWarning()<<"icons: dif/texenv unavailable";return false;}
    diffuse_=texture(gl,diffuse.flipped(Qt::Vertical),true);
    environment_=texture(gl,environment,true);
    // CookieCutter target: the 128x128 px (1080p) window around an icon.
    gl.glGenTextures(1,&cut_);gl.glBindTexture(GL_TEXTURE_2D,cut_);
    gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    gl.glGenFramebuffers(1,&cutFbo_);gl.glBindFramebuffer(GL_FRAMEBUFFER,cutFbo_);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,cut_,0);
    gl.glBindFramebuffer(GL_FRAMEBUFFER,0);gl.glBindTexture(GL_TEXTURE_2D,0);
    // Quad: position (+-0.5, +-0.5, 0, 1) at attribute 0, colour at 3, uv at 8. The icontex maps are stored bottom-up,
    // so the top edge samples t = 1.
    static const float quad[]={
        -.5f,-.5f,0,1, 1,1,1,1, 0,0,0,0,  .5f,-.5f,0,1, 1,1,1,1, 1,0,0,0,  .5f,.5f,0,1, 1,1,1,1, 1,1,0,0,
        -.5f,-.5f,0,1, 1,1,1,1, 0,0,0,0,  .5f,.5f,0,1, 1,1,1,1, 1,1,0,0, -.5f,.5f,0,1, 1,1,1,1, 0,1,0,0};
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    const GLsizei stride=12*sizeof(float);
    gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,stride,nullptr);
    gl.glEnableVertexAttribArray(3);gl.glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(4*sizeof(float)));
    gl.glEnableVertexAttribArray(8);gl.glVertexAttribPointer(8,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(8*sizeof(float)));
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);
    ready_=true;
    qInfo()<<"icons: firmware icon renderer ready";
    return true;
}

void IconRenderer::render(QOpenGLFunctions_3_3_Core& gl,const QVector<Icon>& icons,double seconds,float light,int pixelWidth,int pixelHeight,GLuint targetFbo) {
    if(!ready_ || icons.isEmpty())return;
    const double scale=pixelHeight/1080.;
    const double offsetX=(pixelWidth-1920*scale)/2; // wider-than-16:9 windows centre the 1920-wide layout
    // Light directions from ICONS.mnu angles (degrees).
    const auto direction=[&](const char* z,const char* x){
        const double angleZ=qDegreesToRadians(double(value(z,light))),angleX=qDegreesToRadians(double(value(x,light)));
        return QVector3D(float(std::sin(angleZ)*std::cos(angleX)),float(std::sin(angleZ)*std::sin(angleX)),float(std::cos(angleZ)));
    };
    const QVector3D l1=direction("LIT1 ANG Z","LIT1 ANG X"),l2=direction("LIT2 ANG Z","LIT2 ANG X");
    const float expose=value("EXPOSE",light),refract=value("REFR SCL",light);
    const double envAngle=qDegreesToRadians(-147.+30.*seconds);
    QVector<QVector4D> fc=fragmentCg_.constants;
    const auto set=[&](const char* name,const QVector4D& v){for(int slot:fragmentCg_.parameters.value(name).constantSlots)if(slot<fc.size())fc[slot]=v;};
    // Diagnosis: XMB_ICON_UCODE=<RSX capture of this program> uses the firmware's own constants for everything except
    // the per-icon _Eye/_Ambiant (set below), to compare against the values derived here.
    static const QVector<QVector4D> captured=[&]{
        QFile file(qEnvironmentVariable("XMB_ICON_UCODE"));
        if(!file.open(QIODevice::ReadOnly))return QVector<QVector4D>();
        QByteArray binary=assets_->qglFile("icons","lib/icons/icons_no_ss_no_shad.fpo");
        const QByteArray ucode=file.readAll();
        binary.replace(int(qFromBigEndian<quint32>(binary.constData()+28)),ucode.size(),ucode);
        const auto program=CgProgram::load(binary);
        qInfo()<<"icons: using captured constants from"<<file.fileName()<<program.constants.size();
        return program.constants;
    }();
    const bool useCaptured=captured.size()==fc.size();
    set("_PackedLightParams[0]",{value("ATTN DIFF",light),l1.x(),l2.x(),0});
    set("_PackedLightParams[1]",{value("ATTN SPEC1",light),l1.y(),l2.y(),0});
    set("_PackedLightParams[2]",{value("ATTN SPEC2",light),l1.z(),l2.z(),0});
    set("_PackedLightParams[3]",{value("ATTN ENV",light),value("SPEC1 POW",light),value("SPEC2 POW",light),0});
    set("_RefractParams",{value("REFR R",light)*refract,value("REFR G",light)*refract,value("REFR B",light)*refract,value("REFR TWEAK",light)});
    set("_ParallaxAndGlow",{expose,expose!=0?1.f/expose:0.f,value("PAR BIAS",light),value("PAR SCL",light)});
    set("_EnvRot",{float(std::cos(envAngle)),float(std::sin(envAngle)),1,1});
    // Captured at night (October, "Original" theme); the day values are not captured yet.
    set("_ChangingColor",{0.809361f,0.699557f,0.353068f,value("TIME COLOR MIX",light)});
    set("_LuminanceParams[0]",{0.544441f,1,-0.170123f,0});
    set("_LuminanceParams[1]",{0.20443f,1,0.104857f,0});
    set("_LuminanceParams[2]",{0.20443f,0.544441f,0,0});
    if(useCaptured)fc=captured;
    std::vector<QVector4D> vc(468);
    for(const auto& p:vertexCg_.parameters)if(p.resourceIndex>=0 && p.resourceIndex<468 && !p.defaults.isEmpty())
        vc[p.resourceIndex]=QVector4D(p.defaults.value(0),p.defaults.value(1),p.defaults.value(2),p.defaults.value(3));
    vc[466]=QVector4D(15,8.4375f,0,0); // CookieCutterRatio: the 128x128 px window as a fraction of the screen
    gl.glDisable(GL_DEPTH_TEST);
    for(const auto& icon:icons) {
        const GLuint normals=normalMap(gl,icon.normalMap);
        if(!normals || icon.opacity<=0)continue;
        const double ndcX=icon.centre.x()/960.-1,ndcY=1-icon.centre.y()/540.;
        const double worldX=ndcX*kDistance/kScaleX,worldY=ndcY*kDistance/kScaleY;
        // 1. CookieCutter: the frame behind the icon, 128x128 reference px, into the background map.
        const double cx=offsetX+icon.centre.x()*scale,cy=pixelHeight-icon.centre.y()*scale,half=64*scale;
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,targetFbo);gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,cutFbo_);
        gl.glBlitFramebuffer(GLint(cx-half),GLint(cy-half),GLint(cx+half),GLint(cy+half),0,0,128,128,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        // 2. The icon quad.
        gl.glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);
        gl.glViewport(GLint(offsetX),0,GLsizei(1920*scale),pixelHeight);
        vc[256]=QVector4D(float(kScaleX*icon.size),0,0,float(kScaleX*worldX));
        vc[257]=QVector4D(0,float(kScaleY*icon.size),0,float(kScaleY*worldY));
        vc[258]=QVector4D(0,0,-1,float(kDistance-19.95));
        vc[259]=QVector4D(0,0,-1,float(kDistance));
        vc[467]=QVector4D(float(ndcX),float(ndcY),0,0);
        set("_Eye",{float(-worldX-400),float(-worldY),float(kDistance),float(icon.size)});
        set("_Ambiant",{value("AMB R",light),value("AMB G",light),value("AMB B",light),float(icon.brightness)});
        program_.bind();
        program_.setUniformValueArray("vc",vc.data(),int(vc.size()));program_.setUniformValueArray("fc",fc.constData(),fc.size());
        program_.setUniformValue("tex0",0);program_.setUniformValue("tex1",1);program_.setUniformValue("tex2",2);program_.setUniformValue("tex3",3);
        gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,normals);
        gl.glActiveTexture(GL_TEXTURE1);gl.glBindTexture(GL_TEXTURE_2D,diffuse_);
        gl.glActiveTexture(GL_TEXTURE2);gl.glBindTexture(GL_TEXTURE_2D,environment_);
        gl.glActiveTexture(GL_TEXTURE3);gl.glBindTexture(GL_TEXTURE_2D,cut_);
        program_.setUniformValue("layoutOpacity_",float(icon.opacity));
        gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        gl.glBindVertexArray(vao_);gl.glDrawArrays(GL_TRIANGLES,0,6);gl.glBindVertexArray(0);
        gl.glDisable(GL_BLEND);
        program_.release();
    }
    for(int unit=3;unit>=0;--unit){gl.glActiveTexture(GL_TEXTURE0+unit);gl.glBindTexture(GL_TEXTURE_2D,0);}
    gl.glViewport(0,0,pixelWidth,pixelHeight);
}
}
