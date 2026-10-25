#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <array>
#include <memory>
#include <vector>
#include <QVector3D>
#include <QVector4D>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>

namespace xmb {
// The original XMB ribbon ("lines" in qgl/lines.qrc), ported from the firmware end to end:
//   custom_render_plugin (PPU)  a 19x19 mass-spring cloth (FUN_0004bed0 step, FUN_0004bce0 edge driver,
//                               FUN_0004f814 frame interpolation and y/z swell), started from the plugin's rest
//                               state (preset 1 tables), and ffd_shader1 rendered over an 8x4x4 node grid and read
//                               back into an 11x7x7 deformation lattice (FUN_0004a220, FUN_00047b1c)
//   spurs/moyou/spline (SPU)    per-point FFD (B-spline over that lattice), the camera matrix (LINE1.mnu POS/ANG),
//                               and the bicubic B-spline sheet: 128x128 positions in clip space and normals
//   lines1.vpo/.fpo             the ~400 fine lines: stripe profile, fresnel LUT, edge fade, brightness
//   LinesController             decode of the summed line intensity and the horizontal colour gradient
// All coefficients come from LINE1.mnu (override/day and override/night). Derivation: audit/PIPELINE.md.
class Ribbon {
public:
    // Requires a current GL context. Returns false (and logs why) when the firmware programs are unavailable.
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // Adds the ribbon to targetFbo (additive). seconds: wall time; dayFraction: local time of day, 0..1 from midnight.
    void render(QOpenGLFunctions_3_3_Core& gl,double seconds,double dayFraction,int pixelWidth,int pixelHeight,GLuint targetFbo);
    // Crest of the ribbon centre line as a fraction of screen height from the top, sampled left to right across
    // the screen after the last render (for the procedural particles and for measurement).
    const std::array<float,33>& crest() const { return crest_; }
    // The sheet's centre line left to right, placed in the particle camera's view space (fovy 71.846) so that it
    // lands where the ribbon is drawn (the particle emitter).
    const std::vector<QVector3D>& centreLine() const { return centreLine_; }
    // LINE1.mnu, the values the cloth and the spline task read.
    struct Preset {
        float damping=.0001f,length=.306001f,tension=.25f,timestep=2,perturbation=.0998587f,endY=.4f,endZ=.2f,ffdParam=-1.33509f;
        QVector3D scale1{5.67726f,1.00077f,1},scale2{3.2f,1.27579f,2.88782f},offset{0,-.469999f,0};
        QVector3D pos{-8.2f,-1.08844f,-6.40287f},axis{-.994157f,.0867576f,.065364f};
        float angle=18.1208f,spacing=407.658f,brightness=.701917f,mipmapBias=1.86707f,fresnel=.638971f;
        Preset mix(const Preset& day,float t) const;
    };
private:
    static constexpr int side_=19,points_=side_*side_,grid_=128;
    struct Cloth {
        std::array<QVector4D,points_> pos{},prev{},vel{},drawn{};
        float target=0,phase=0,acc=0;int seed=0;
        void step(const Preset& p);
        void swell(const Preset& p);
        QVector3D lattice(int i) const { return drawn[size_t(i)].toVector3D(); }
        float random();
        void spring(int i,int j,float rest,float k);
    };
    bool ready_=false;
    Preset coldboot1_,coldboot2_,night_,yoake_,day_,higure_,current_;
    double bootSeconds_=0;Preset bootSnapshot_;bool snapshotTaken_=false;
    Preset clockPreset(double dayFraction) const;
    void advancePresets(double dt,double dayFraction);
    Cloth cloth_;
    double lastSeconds_=-1;
    CgProgram ffdCg_,linesVertexCg_,linesFragmentCg_;
    QOpenGLShaderProgram ffd_,lines_,composite_;
    std::unique_ptr<QOpenGLTexture> uvw_,stripes_,fresLut_,encode_;
    std::unique_ptr<QOpenGLFramebufferObject> field_,sum_;
    GLuint vao_=0,vbo_=0,ibo_=0,emptyVao_=0;
    GLsizei indexCount_=0;
    std::array<float,4*128> fieldPixels_{};
    std::array<QVector4D,539> deform_{};
    std::array<QVector4D,points_> clipLattice_{};
    std::vector<float> vertices_;
    std::array<float,33> crest_{};
    std::vector<QVector3D> centreLine_;
    void deformLattice(QOpenGLFunctions_3_3_Core& gl,const Preset& p);
    void splineSheet(const Preset& p,float aspect);
};
}
