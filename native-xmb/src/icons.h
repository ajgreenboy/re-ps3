#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <memory>
#include <QHash>
#include <QPointF>
#include <QDateTime>
#include <QVector3D>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>

namespace xmb {
// The firmware's glass icons (qgl/icons.qrc), drawn with its own programs: quad.vpo and icons_no_ss_no_shad.fpo, the
// variant the firmware runs at 1920x1080 with ICONS.mnu SHADOW STREN 0 (captured from RPCS3, audit/PIPELINE.md).
// Per icon the shader reads the icontex normal map (tex0), icontex "dif" (tex1), widgets/other/texenv.dds rotated by
// _EnvRot (tex2) and a 128x128 px cut of the frame behind the icon (tex3, glutils CookieCutter), which it refracts.
//
// Uniforms follow the capture of custom_render_plugin's values (fragment constants patched into the microcode):
//   _PackedLightParams[i] = (attenuation i, light1.i, light2.i, 0) for i = DIFF, SPEC1, SPEC2; [3] = (ENV, SPEC1 POW,
//                           SPEC2 POW, 0); light = (sin Z cos X, sin Z sin X, cos Z) from LITn ANG Z/X (degrees)
//   _Ambiant              = (AMB R, AMB G, AMB B, icon brightness: 0.698 unfocused, 1 focused)
//   _RefractParams        = (REFR R, G, B x REFR SCL, REFR TWEAK);  _ParallaxAndGlow = (EXPOSE, 1/EXPOSE, PAR BIAS, PAR SCL)
//   _EnvRot               = (cos a, sin a, 1, 1), a turning ~30 deg/s
//   _Eye                  = (-X - 400, -Y, d, size): camera relative to the icon; d = 2700, ndc = (2.8125 X, 5 Y) / d
//   _ChangingColor: repeat-bilinear amb.bmp sample at time of day and seasonal month fraction
//   _LuminanceParams: Lagrange coefficients from firmware control points scaled by preset GLASS
class IconRenderer {
public:
    struct Icon {
        QString normalMap;   // icontex name, e.g. "6n"
        QPointF centre;      // 1920x1080 reference pixels
        double size=117.13;  // firmware units (= reference pixels at d 2700): 117.13 unfocused, 167.33 focused
        double brightness=.698;
        double opacity=1;
        double distance=2700; // camera distance: 2700, or 2857.2 for a parent column behind an open page
    };
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    void setClock(const QDateTime& when) { clock_=when; }
    // Draws the icons onto targetFbo, cutting each one's background from that same framebuffer first.
    // light: 0 night .. 1 day (the background's NightDayBlend preset blend).
    void render(QOpenGLFunctions_3_3_Core& gl,const QVector<Icon>& icons,double seconds,float light,int pixelWidth,int pixelHeight,GLuint targetFbo);
private:
    bool ready_=false;
    Assets* assets_=nullptr;
    CgProgram vertexCg_,fragmentCg_;
    QOpenGLShaderProgram program_;
    GLuint vao_=0,vbo_=0,diffuse_=0,environment_=0,cut_=0,cutFbo_=0;
    QHash<QString,GLuint> normalMaps_;
    QHash<QString,float> day_,night_;
    QImage ambientPalette_;
    QDateTime clock_;
    QVector3D changingColour(const QDateTime& when) const;
    GLuint texture(QOpenGLFunctions_3_3_Core& gl,const QImage& image,bool mipmaps);
    GLuint normalMap(QOpenGLFunctions_3_3_Core& gl,const QString& name);
    float value(const QString& key,float light) const;
};
}
