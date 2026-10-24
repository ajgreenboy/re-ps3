#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <array>
#include <memory>
#include <vector>
#include <QVector3D>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>

namespace xmb {
// The original XMB ribbon ("lines" in qgl/lines.qrc), drawn with the firmware's own programs:
//   ffd_shader1.fpo   the wave: a deformation field over (along, across, time), rendered to a float target and read back
//   lines1.vpo/.fpo   the sheet of ~400 fine lines: stripe profile, fresnel LUT, edge fade, brightness
//   LinesController   decode of the summed line intensity and the horizontal colour gradient (done here in one pass)
// Textures follow the custom_render_plugin generators (Stripes, Encode) and freslut1.tga. The SPU side
// (spurs/moyou/spline/spline.elf: spring lattice, camera placement) is reimplemented: the sheet follows the FFD
// field directly and its placement on screen is fitted to the reference video (audit/VIDEO-REFERENCES.md).
class Ribbon {
public:
    // Requires a current GL context. Returns false (and logs why) when the firmware programs are unavailable.
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // Adds the ribbon to targetFbo (additive). seconds: wall time; light: 0 night .. 1 day.
    void render(QOpenGLFunctions_3_3_Core& gl,double seconds,float light,int pixelWidth,int pixelHeight,GLuint targetFbo);
    // Crest of the ribbon centre line as a fraction of screen height from the top, sampled left to right across
    // the screen after the last render (for the procedural particles and for measurement).
    const std::array<float,33>& crest() const { return crest_; }
    // The sheet's centre line in view space after the last render, left to right (the particle emitter).
    const std::vector<QVector3D>& centreLine() const { return centreLine_; }
private:
    struct Preset { float brightness,fresnel,mipmapBias,spacing; };
    static constexpr int along_=128,across_=24;
    bool ready_=false;
    CgProgram ffdCg_,linesVertexCg_,linesFragmentCg_;
    QOpenGLShaderProgram ffd_,lines_,composite_;
    std::unique_ptr<QOpenGLTexture> uvw_,stripes_,fresLut_,encode_;
    std::unique_ptr<QOpenGLFramebufferObject> field_,sum_;
    GLuint vao_=0,vbo_=0,ibo_=0,emptyVao_=0;
    GLsizei indexCount_=0;
    std::vector<float> fieldPixels_,vertices_;
    std::array<float,33> crest_{};
    std::vector<QVector3D> centreLine_;
};
}
