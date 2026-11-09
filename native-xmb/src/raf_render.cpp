#include "raf_render.h"
#include "p3t.h"
#include "edge_decode.h"
#include "gtf_decode.h"
#include <QHash>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QtMath>
#include <cstddef>

namespace xmb {
namespace {
QVector3D vec(const P3t::Raf::Vec3& v) { return {float(v.x),float(v.y),float(v.z)}; }
struct Mesh { GLuint vao=0,vbo=0,ibo=0; int count=0; };
}
QImage renderRafStill(const QByteArray& raf,const QString& devFlash,int width,int height,QString* error,RafStillStats* stats) {
    Q_UNUSED(devFlash); // The generic shader does not depend on firmware shader archives.
    if(error)error->clear();
    if(stats)*stats={};
    const auto fail=[&](const QString& message)->QImage { if(error)*error=message;return {}; };
    if(width<=0 || height<=0)return fail(QStringLiteral("invalid image size"));
    QString why;const P3t theme=P3t::read(raf,&why);
    if(theme.isNull() || !theme.hasScene())return fail(QStringLiteral("invalid RAF: ")+why);
    const auto& scene=theme.scene();
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);
    QOpenGLContext context;context.setFormat(format);
    if(!context.create())return fail(QStringLiteral("cannot create OpenGL context"));
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
    if(!surface.isValid() || !context.makeCurrent(&surface))return fail(QStringLiteral("cannot make offscreen context current"));
    QOpenGLFunctions_3_3_Core gl;
    if(!gl.initializeOpenGLFunctions())return fail(QStringLiteral("OpenGL 3.3 core unavailable"));
    QOpenGLFramebufferObjectFormat fboFormat;fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject fbo(width,height,fboFormat);
    if(!fbo.isValid() || !fbo.bind())return fail(QStringLiteral("cannot create framebuffer"));
    QOpenGLShaderProgram program;
    const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(location=2) in vec2 uv;
uniform mat4 mvp,model; uniform mat3 normalMatrix; uniform vec4 uvTransform;
out vec3 worldPos,worldNormal; out vec2 texCoord;
void main(){ vec4 p=model*vec4(position,1); worldPos=p.xyz; worldNormal=normalMatrix*normal;
 texCoord=uv*uvTransform.zw+uvTransform.xy; gl_Position=mvp*vec4(position,1); })";
    const char* fragment=R"(#version 330 core
in vec3 worldPos,worldNormal; in vec2 texCoord; out vec4 outColor;
uniform sampler2D diffuseMap; uniform vec4 actorColor; uniform vec3 lightPos,lightColor,attenuation,ambient;
void main(){ vec4 tex=texture(diffuseMap,texCoord); vec3 delta=lightPos-worldPos; float d=length(delta);
 float lambert=max(dot(normalize(worldNormal),normalize(delta)),0.0);
 float falloff=1.0/max(dot(attenuation,vec3(1,d,d*d)),0.001);
 vec3 lighting=ambient+lightColor*lambert*falloff;
 outColor=vec4(tex.rgb*actorColor.rgb*lighting,tex.a*actorColor.a); })";
    if(!program.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) || !program.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !program.link())return fail(QStringLiteral("shader: ")+program.log());
    QHash<QString,Mesh> meshes;QHash<QString,GLuint> textures;
    const auto cleanup=[&] {
        for(const auto& m:meshes){gl.glDeleteBuffers(1,&m.vbo);gl.glDeleteBuffers(1,&m.ibo);gl.glDeleteVertexArrays(1,&m.vao);}
        for(GLuint id:textures)gl.glDeleteTextures(1,&id);
    };
    int triangles=0;
    for(const auto& model:scene.models) {
        QVector<EdgeVertex> vertices;QVector<quint32> indices;
        if(!decodeEdge(model.geometryData,vertices,indices,&why)){cleanup();return fail(QStringLiteral("model %1: %2").arg(model.id,why));}
        Mesh m;m.count=int(indices.size());triangles+=m.count/3;
        gl.glGenVertexArrays(1,&m.vao);gl.glGenBuffers(1,&m.vbo);gl.glGenBuffers(1,&m.ibo);gl.glBindVertexArray(m.vao);
        gl.glBindBuffer(GL_ARRAY_BUFFER,m.vbo);gl.glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(EdgeVertex),vertices.constData(),GL_STATIC_DRAW);
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.ibo);gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(quint32),indices.constData(),GL_STATIC_DRAW);
        gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(EdgeVertex),reinterpret_cast<void*>(offsetof(EdgeVertex,position)));
        gl.glEnableVertexAttribArray(1);gl.glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(EdgeVertex),reinterpret_cast<void*>(offsetof(EdgeVertex,normal)));
        gl.glEnableVertexAttribArray(2);gl.glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(EdgeVertex),reinterpret_cast<void*>(offsetof(EdgeVertex,uv)));
        meshes.insert(model.id,m);
    }
    for(const auto& texture:scene.textures) {
        QImage image=decodeGtf(texture.data,&why).convertToFormat(QImage::Format_RGBA8888);
        if(image.isNull()){cleanup();return fail(QStringLiteral("texture %1: %2").arg(texture.id,why));}
        GLuint id=0;gl.glGenTextures(1,&id);gl.glBindTexture(GL_TEXTURE_2D,id);
        gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.width(),image.height(),0,GL_RGBA,GL_UNSIGNED_BYTE,image.constBits());
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        textures.insert(texture.id,id);
    }
    QHash<QString,QString> materials;for(const auto& material:scene.materials)materials.insert(material.id,material.colorMap);
    const auto& c=scene.camera;QVector3D eye=vec(c.position),direction=vec(c.direction),up=vec(c.up);
    const bool cameraFallback=!c.present || c.yfov<=0 || c.yfov>=3.14159 || c.znear<=0 || c.zfar<=c.znear || direction.lengthSquared()<1e-8f || up.lengthSquared()<1e-8f || QVector3D::crossProduct(direction,up).lengthSquared()<1e-8f;
    // RAF camera vectors can be absent; use a perspective view of the origin in that case.
    if(cameraFallback){eye={0,0,4};direction={0,0,-1};up={0,1,0};}
    QMatrix4x4 projection,view;projection.perspective(cameraFallback?45.f:qRadiansToDegrees(float(c.yfov)),float(width)/height,cameraFallback?.01f:float(c.znear),cameraFallback?1000.f:float(c.zfar));view.lookAt(eye,eye+direction,up);
    QVector3D lightPos(0,2,4),lightColor(1,1,1),attenuation(1,0,0),ambient(.2f,.2f,.2f);bool lightFallback=true;
    // Light type 0 is the point light seen in coldboot; unknown types fall back to a white point light.
    for(const auto& light:scene.lights)if(light.type==0 && vec(light.color).lengthSquared()>0){lightPos=vec(light.position);lightColor=vec(light.color);attenuation=vec(light.attenuation);if(attenuation.lengthSquared()<1e-8f)attenuation={1,0,0};lightFallback=false;break;}
    gl.glViewport(0,0,width,height);gl.glClearColor(0,0,0,1);gl.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gl.glDisable(GL_CULL_FACE);gl.glDisable(GL_DEPTH_TEST);gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    program.bind();program.setUniformValue("diffuseMap",0);program.setUniformValue("lightPos",lightPos);program.setUniformValue("lightColor",lightColor);program.setUniformValue("attenuation",attenuation);program.setUniformValue("ambient",ambient);
    for(const auto& actor:scene.actors) {
        if(!meshes.contains(actor.model) || !materials.contains(actor.material) || !textures.contains(materials.value(actor.material))){cleanup();return fail(QStringLiteral("actor %1 has unresolved model or diffuse texture").arg(actor.id));}
        QMatrix4x4 model;model.translate(vec(actor.position));model.rotate(qRadiansToDegrees(float(actor.rotation.z)),0,0,1);model.rotate(qRadiansToDegrees(float(actor.rotation.y)),0,1,0);model.rotate(qRadiansToDegrees(float(actor.rotation.x)),1,0,0);model.scale(vec(actor.scale));
        program.setUniformValue("model",model);program.setUniformValue("mvp",projection*view*model);program.setUniformValue("normalMatrix",model.normalMatrix());
        program.setUniformValue("uvTransform",QVector4D(float(actor.uvOffset.x),float(actor.uvOffset.y),float(actor.uvScale.x),float(actor.uvScale.y)));
        program.setUniformValue("actorColor",QVector4D(float(actor.color.x),float(actor.color.y),float(actor.color.z),float(actor.color.w)));
        gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,textures.value(materials.value(actor.material)));const Mesh m=meshes.value(actor.model);gl.glBindVertexArray(m.vao);gl.glDrawElements(GL_TRIANGLES,m.count,GL_UNSIGNED_INT,nullptr);
    }
    gl.glBindVertexArray(0);program.release();QImage result=fbo.toImage();cleanup();
    if(stats){stats->actors=int(scene.actors.size());stats->models=int(meshes.size());stats->textures=int(textures.size());stats->triangles=triangles;stats->cameraFallback=cameraFallback;stats->lightFallback=lightFallback;}
    return result;
}
}
