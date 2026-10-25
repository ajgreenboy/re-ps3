#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>

namespace xmb {
// The background particles, after linkev/PlayStation-3-XMB's particle layer (ps3xmbwave/particles.js, MIT,
// THIRD-PARTY.md): 2000 additive point sprites whose screen position, size and twinkle are closed-form functions of a
// per-particle random seed and time, so nothing is simulated and nothing can jump between frames. Not a firmware port:
// particles.elf (the SPU simulation) and the particles_quads/particles_second programs are not used.
class Particles {
public:
    bool init(QOpenGLFunctions_3_3_Core& gl);
    bool ready() const { return ready_; }
    // Adds the sparkles to the bound framebuffer (after the background's tone mapping, as the reference layers them).
    void render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight);
    // PARTICLES.mnu GLOBAL ALPHA during the cold boot: 0 until NormalBG2, then up to 1 (RPCS3 capture, coldboot.h).
    void setGlobalAlpha(float alpha) { globalAlpha_=alpha; }
private:
    bool ready_=false;
    QOpenGLShaderProgram program_;
    GLuint vao_=0,vbo_=0;
    int count_=0;
    float globalAlpha_=1;
    double timeOffset_=0;
};
}
