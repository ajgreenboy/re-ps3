#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <functional>
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
//
// Every number comes from HDR.mnu at run time (P1): override/night, override/day and override/coldboot1
// through the shared Materials reader, TEX SIZE / TEX MAX MIP from the base file. The compiled values are
// fallbacks for a firmware tree without vsh/resource/qgl/lines.qrc (logged), never the source.
class Hdr {
public:
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // Binds the float scene target (cleared). Returns its framebuffer id for passes that rebind their own.
    GLuint begin(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight);
    // Tone maps the scene with glare into targetFbo. nightDayBlend: 0 night preset .. 1 day preset.
    // extraGlare, when set, draws additively into the glare source (bound, viewport set) after the threshold pass,
    // so that it is blurred with the rest of the glare (the particles' particles_second halos).
    void finish(QOpenGLFunctions_3_3_Core& gl,float nightDayBlend,GLuint targetFbo,const std::function<void(int width,int height)>& extraGlare={});
    // Cold boot: HDR.mnu coldboot1 -> coldboot2 -> the clock's night/day blend, by Ribbon::bootWeights.
    void setBoot(float toColdboot2,float toClock) { toColdboot2_=toColdboot2;toClock_=toClock; }
private:
    // One HDR.mnu preset. The initializers are the base file's day values, used only when the MNU
    // cannot be read (fallback: HDR.mnu).
    struct Preset {
        float exposure=1.05f;      // fallback: EXPOSURE
        float white=0.899181f;     // fallback: WHITE LEVEL
        float glareLevel=1.10245f; // fallback: GLARE LEVEL
        float threshold=0.738857f; // fallback: GLARE THRESH
        float radius[3]={1.23888f,1.43176f,1.55787f}; // fallback: GAUSSIAN RAD R/G/B
        float sumPow=0.557478f;    // fallback: GLARE SUM POW
    };
    struct Pass { CgProgram cg; QOpenGLShaderProgram program; };
    bool load(Assets& assets,Pass& pass,const char* fragment,const char* vertexName=nullptr);
    void setFc(Pass& pass,QVector<QVector4D>& fc,const char* name,const QVector4D& value);
    bool ready_=false;
    float toColdboot2_=1,toClock_=1;
    Preset night_,day_,coldboot1_;
    int texSize_=8,texMaxMip_=8;   // fallback: HDR.mnu TEX SIZE / TEX MAX MIP
    Pass toneMap_,glareSource_,gaussian_,accGlare_,applyGlare_;
    std::unique_ptr<QOpenGLFramebufferObject> scene_,toned_,source_,accumulated_;
    std::vector<std::unique_ptr<QOpenGLFramebufferObject>> blurX_,blurY_; // one pair per mip level
    GLuint emptyVao_=0,gaussianVao_=0,gaussianVbo_=0;
    int width_=0,height_=0;
};
}
