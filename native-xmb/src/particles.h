#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <memory>
#include <random>
#include <vector>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QQuaternion>
#include <QVector3D>

namespace xmb {
// The original background particles ("particles" in qgl/lines.qrc), drawn with the firmware's programs
// lib/particles/particles_quads.vpo/.fpo: quads rotated per particle, sized and darkened by depth of field
// (near / middle / far sizes, focus distances and curves), turned towards the camera when near, lit by the spot
// light with lambert + specular over the iridescent texture, and cut to a soft disc that widens with blur.
// The simulation itself (spurs/particles/particles/particles.elf) is reimplemented on the CPU from PARTICLES.mnu:
// emission from the ribbon, aging, friction, brownian motion, gravity and spin.
class Particles {
public:
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // Emitter: the ribbon's centre line in view space (left to right). Advances the simulation to `seconds`.
    void update(double seconds,const std::vector<QVector3D>& emitter);
    // Adds the particles to the currently bound target (additive).
    void render(QOpenGLFunctions_3_3_Core& gl,float light,int pixelWidth,int pixelHeight);
private:
    struct Particle { QVector3D position,velocity; QQuaternion rotation; QVector3D spinAxis; float spin=0,age=0,agingRate=0; };
    bool ready_=false;
    CgProgram vertexCg_,fragmentCg_;
    QOpenGLShaderProgram program_;
    std::unique_ptr<QOpenGLTexture> iridescent_;
    GLuint vao_=0,vbo_=0,ibo_=0;
    std::vector<Particle> particles_;
    std::vector<float> vertices_;
    std::mt19937 random_{0x5053u};
    double simulated_=-1;
};
}
