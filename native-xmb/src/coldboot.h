#pragma once
#include "assets.h"
#include "rsx_program.h"
#include <QAudioFormat>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QVector4D>
#include <array>
#include <memory>
#include <vector>

class QAudioDecoder;
class QAudioSink;
class QBuffer;

namespace xmb {
// The 4.93 cold-boot intro, from the firmware's own files (audit/PIPELINE.md "Cold boot").
// custom_render_plugin's boot case (0x15a00) opens page_coldboot and runs anim_coldboot2, starts raf.sprx on
// vsh/resource/coldboot.raf (FUN_000180c8 / FUN_00017fc8) and plays coldboot_stereo.ac3, all in the same call. The
// RAF is a small scene: six actors on one plane mesh (HD and SD copies of logo, footer and blur), a camera, a point and
// an ambient light, textures as GTF, and coldboot.jsx (VSMX bytecode) as the script that animates them.
// Here: the HD actors drawn with the firmware's raf.qrc vert0.vpo + sce01.fpo, the script's timeline with its timing
// constants read from the VSMX, and anim_coldboot2's plugin events.
class Coldboot {
public:
    // anim_coldboot2 (custom_render_plugin.rco), event table at 0x9cbc0: BootBG2 at 0 s (0x110dc: LINE/BACKGROUND/
    // HDR/PARTICLES coldboot1 at once, then coldboot2 over 3 s), NormalBG2 at 4 s (0x11b20: the clock keyframes over
    // 7.5 s), ShowGUI at 5.5 s (0x4110), Finished at 10 s (0x3c10: page_coldboot closed).
    static constexpr double normalBg=4.0,showGui=5.5,finished=10.0;
    Coldboot();
    ~Coldboot();
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // The AC3 decode has finished (or failed): start() can begin with the sound.
    bool soundReady() const { return soundReady_; }
    void start();
    // A button press while the boot holds the GUI: before ShowGUI it skips the intro (a port convenience, the
    // notice included); during the health notice it is the notice's own Cross/Circle handler, honoured only once
    // the input lock has run out (sysconf_plugin 0x5ab6c / 0x5abcc test the lock flag at +0xbd).
    void skip();
    // Seconds since start (plugin time); a large value once skipped or never started.
    double seconds() const;
    bool logoVisible() const { return ready_ && seconds()<scriptEnd_; }
    // The health warning (F1, audit/PIPELINE.md "Cold boot"): shown from ShowGUI until it closes; off with
    // --no-health-warning or settings.ini [boot] healthWarning=false. Set before init().
    void setNoticeEnabled(bool enabled) { noticeEnabled_=enabled; }
    // The boot still owns the screen: the intro before ShowGUI, then the notice until its close fade ends.
    bool holdsGui() const;
    void render(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight,GLuint targetFbo);
private:
    struct Actor { QString name; QVector3D position,scale,rotation; QVector2D uvOffset,uvScale; GLuint texture=0; };
    // A property animation as the RAF script runtime runs it: from the value at the call to the target over duration,
    // linear or a cubic-Bezier time curve (x1, y1, x2, y2).
    struct Track { QVector4D from,to; double start=0,duration=0; QVector4D curve; bool curved=false; QVector4D at(double t) const; };
    bool ready_=false,started_=false,skipped_=false,soundReady_=false;
    QElapsedTimer clock_;
    double scriptEnd_=0;
    CgProgram vertexCg_,fragmentCg_;
    QOpenGLShaderProgram program_;
    GLuint vao_=0,vbo_=0,warp_=0,white_=0;
    std::array<Actor,3> actors_;               // logo, footer, blur (HD)
    QVector3D eye_,target_,up_;float yfov_=0,znear_=0,zfar_=0;
    QVector3D lightAttenuation_;               // the RAF point light's initial attenuation
    // coldboot.jsx timing (seconds) and derived start times.
    double firstWait_=0,appearDuration_=0,appearWait_=0,blurDuration_=0,blurWait_=0,wholeDuration_=0,wholeWait_=0,fadeDuration_=0;
    double appearStart_=0,blurStart_=0,wholeStart_=0,fadeStart_=0;
    QByteArray pcm_;QAudioFormat pcmFormat_;
    std::unique_ptr<QAudioDecoder> decoder_;std::unique_ptr<QAudioSink> sink_;std::unique_ptr<QBuffer> pcmBuffer_;
    GLsizei indexCount_=0;GLuint ibo_=0;
    bool parseRaf(const QByteArray& raf,QOpenGLFunctions_3_3_Core& gl);
    bool parsePlane(const QByteArray& edge);
    std::vector<float> planeVertices_;std::vector<GLushort> planeIndices_;
    bool readScript(const QByteArray& vsmx);
    void initLighting(QOpenGLFunctions_3_3_Core& gl);
    void decodeSound(const QString& path);
    // The script's state at t: actor colours, light position, colour and attenuation.
    void state(double t,std::array<QVector4D,3>& colours,QVector3D& lightPosition,QVector3D& lightColour,QVector3D& attenuation) const;
    // Health notice. sysconf_plugin FUN_00056f48 picks the string and two timers by region: SCEE regions
    // (codes 2, 4, 6, 9) msg_health_warning_start_scee, input locked 3000 ms, auto-close at 5000 ms; the others
    // msg_health_warning_start with 1000 / 3000 ms (or 1000 / 5000 for an unlisted code). Either close path hides
    // the widget over 200 ms (0x14b4fc, 0x14b500/4). Times here in seconds of plugin time from ShowGUI.
    bool noticeEnabled_=true;
    QString noticeText_,noticeFamily_;
    double noticeLock_=3.0,noticeHold_=5.0,noticeCloseAt_=0;
    static constexpr double noticeFade=0.2;
    bool noticeShown() const { return noticeEnabled_ && !noticeText_.isEmpty(); }
    double noticeAlpha(double t) const;
    void renderNotice(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight,GLuint targetFbo,double alpha);
};
}
