#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <memory>
#include <vector>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>

namespace xmb {
// HDR pass of the original background (qgl/lines.qrc HDR.mnu), with the firmware's glutils programs:
//   ToneMap.fpo            extended Reinhard: L(1 + L/white^2) / (1 + L), L = colour x EXPOSURE
//   GlareSourceNoTone.fpo  max(colour - GLARE THRESH, 0) into a 2^TEX SIZE wide glare source with mips
//   Gaussian.vpo/.fpo      15-tap separable blur per mip level, per-channel radius (GAUSSIAN RAD R/G/B)
//   AccGlare.fpo           weighted sum of the blurred levels
//   ApplyGlare.fpo         tone-mapped screen + glare x GLARE LEVEL
// The plugin stores its frame tone-mapped and recovers HDR with GlareSourcePre (the exact inverse of ToneMap);
// keeping the scene in a float target gives the same values without the round trip.
class Hdr {
public:
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // Binds the float scene target (cleared). Returns its framebuffer id for passes that rebind their own.
    GLuint begin(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight);
    // Tone maps the scene with glare into targetFbo. nightDayBlend: 0 night preset .. 1 day preset.
    void finish(QOpenGLFunctions_3_3_Core& gl,float nightDayBlend,GLuint targetFbo);
private:
    struct Pass { CgProgram cg; QOpenGLShaderProgram program; };
    bool load(Assets& assets,Pass& pass,const char* fragment,const char* vertexName=nullptr);
    void setFc(Pass& pass,QVector<QVector4D>& fc,const char* name,const QVector4D& value);
    bool ready_=false;
    Pass toneMap_,glareSource_,gaussian_,accGlare_,applyGlare_;
    std::unique_ptr<QOpenGLFramebufferObject> scene_,toned_,source_,accumulated_;
    std::vector<std::unique_ptr<QOpenGLFramebufferObject>> blurX_,blurY_; // one pair per mip level
    GLuint emptyVao_=0,gaussianVao_=0,gaussianVbo_=0;
    int width_=0,height_=0;
};
}
