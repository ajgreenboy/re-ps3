#include "particles.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
#include <QDebug>

namespace xmb {
namespace {
// ps3xmbwave/particles-settings.js defaults.
constexpr int count=2000;
constexpr float opacity=.75f,sizeBase=2.6f,sizeVar=1.5f,flowSpeed=.18f;
// ps3xmbwave/particles.js, the point-sprite program, as GLSL 3.30. aSeed = (x, y, size) per particle.
const char* vertexSource=R"(#version 330 core
layout(location=0) in vec3 aSeed;
uniform float uTime;
uniform float flowSpeed;
uniform float ratio;
uniform float ptSizeBase;
uniform float ptSizeVar;
uniform float pixelScale;
out float vAlpha;
void main() {
    gl_PointSize = (aSeed.z * ptSizeVar + ptSizeBase) * pixelScale;
    float time = uTime * flowSpeed;
    float x = fract(time * (aSeed.x - 0.5) / 15.0 + aSeed.y * 50.0) * 2.0 - 1.0;
    float y = sin(sign(aSeed.y) * time * (aSeed.y + 1.5) / 4.0 + aSeed.x * 100.0)
            / ((6.0 - aSeed.x * 4.0 * aSeed.y) / ratio);
    float opVar = mix(
        sin(time * (aSeed.x + 0.5) * 12.0 + aSeed.y * 10.0),
        sin(time * (aSeed.y + 1.5) * 6.0 + aSeed.x * 4.0),
        y * 0.5 + 0.5) * aSeed.x + aSeed.y;
    vAlpha = opVar * opVar * (1.0 - fract(aSeed.x + time * 0.00285));
    gl_Position = vec4(x, y, 0.0, 1.0);
})";
const char* fragmentSource=R"(#version 330 core
in float vAlpha;
out vec4 oColor;
uniform float particleOpacity;
void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float d = dot(c, c);
    if (d > 1.0) discard;
    float sparkle = (1.0 - d) * (1.0 - d);
    float a = vAlpha * particleOpacity * sparkle;
    oColor = vec4(vec3(a), 1.0);
})";
}

bool Particles::init(QOpenGLFunctions_3_3_Core& gl) {
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexSource) || !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentSource) || !program_.link()) {
        qWarning().noquote()<<"particles: link failed:"<<program_.log();return false;
    }
    // Seeds as rebuildParticles makes them: x, y uniform; size pow(u, 8) + 0.1 (mostly small, a few large).
    std::mt19937 random(0x5053u);std::uniform_real_distribution<float> unit(0.f,1.f);
    std::vector<float> seeds(size_t(count)*3);
    for(int i=0;i<count;++i){float* s=&seeds[size_t(i)*3];s[0]=unit(random);s[1]=unit(random);s[2]=std::pow(unit(random),8.f)+.1f;}
    count_=count;
    timeOffset_=unit(random)*1000.; // particlesTimeSec starts at a random point
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(seeds.size()*sizeof(float)),seeds.data(),GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);
    ready_=true;
    qInfo()<<"particles:"<<count_<<"sparkles (linkev/PlayStation-3-XMB particle layer)";
    return true;
}

void Particles::render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight) {
    if(!ready_ || globalAlpha_<=0 || pixelWidth<=0 || pixelHeight<=0)return;
    const float aspect=float(pixelWidth)/float(pixelHeight);
    gl.glViewport(0,0,pixelWidth,pixelHeight);
    gl.glDisable(GL_DEPTH_TEST);gl.glEnable(GL_PROGRAM_POINT_SIZE);
    gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE);
    program_.bind();
    program_.setUniformValue("uTime",float(std::fmod(seconds+timeOffset_,1e5)));
    program_.setUniformValue("flowSpeed",flowSpeed);
    program_.setUniformValue("ratio",std::clamp(aspect,1.f,2.f)*.375f);
    program_.setUniformValue("particleOpacity",opacity*globalAlpha_);
    program_.setUniformValue("ptSizeBase",sizeBase);program_.setUniformValue("ptSizeVar",sizeVar);
    program_.setUniformValue("pixelScale",float(pixelHeight)/1080.f); // the sizes are 1080p pixels
    gl.glBindVertexArray(vao_);gl.glDrawArrays(GL_POINTS,0,count_);gl.glBindVertexArray(0);
    program_.release();
    gl.glDisable(GL_BLEND);gl.glDisable(GL_PROGRAM_POINT_SIZE);
}
}
