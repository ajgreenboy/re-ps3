#pragma once
#include "assets.h"
#include "coldboot.h"
#include "hdr.h"
#include "particles.h"
#include "icons.h"
#include "ribbon.h"
#include "rsx_program.h"
#include "shell.h"
#include "photo_viewport.h"
#include "media_controls.h"
#include "text_renderer.h"
#include <array>
#include <functional>
#include <QDateTime>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLTexture>

namespace xmb {
class View final:public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core {
public:
    View(Shell& shell,Assets& assets,QWidget* parent=nullptr);
    ~View() override;
    static double dayLight(double hour);
    // The XMB's moves are paf animations: 200 ms in accel mode 5 (vsh.elf PhXmBar focus moves FUN_003ad3bc /
    // FUN_003ad728; paf animation setup FUN_003981ec and evaluator FUN_00398748). Mode 5 closes a fixed fraction
    // k of the remaining distance every 60 Hz frame, k from a table by the duration in frames, and lands on the
    // target when the frame-snapped duration ends. A new target restarts the curve from the current value.
    // Mode 4 is the same per-frame form with its own k table; the page (depth) move uses it (RPCS3 capture).
    struct PafEase {
        double value=0;
        void retarget(double target,double milliseconds=200,int mode=5);
        void advance(double seconds);
        bool running() const { return running_; }
    private:
        double from_=0,to_=0,progress_=1,elapsed_=0,duration_=0,k_=0,growth_=1.000001;
        bool started_=false,running_=false;
    };
    static constexpr double moveMilliseconds=200;
    static constexpr double pageMilliseconds=300;
    // Renders the background offscreen at reference times and writes the same statistics measured from the
    // reference videos (band colours by clock time, ribbon crest over 40 s). Returns false on GL failure.
    bool measureReference(const QString& path);
    void setBackgroundClock(const QDateTime& when) { backgroundOverride_=when;update(); }
    // Fixed animation time (seconds) for deterministic A/B captures; -1 follows the real clock.
    void setRenderClock(double seconds) { secondsOverride_=seconds;update(); }
    // Diagnostic focus age for reproducible glow/idle-fade captures; navigation still animates normally.
    void setFocusTime(qint64 milliseconds) { focusTimeOverride_=milliseconds;++uiRevision_;update(); }
    // The 4.93 cold-boot intro before the XMB (coldboot.h); off for measurement and scripted runs. Set before show.
    void setIntroEnabled(bool enabled) { introEnabled_=enabled; }
    // Verification taps dispatch synchronously, independent of foreground focus and the physical input poll.
    void replayKeyTap(int key);
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    Shell& shell_;
    Assets& assets_;
    QOpenGLShaderProgram background_;
    GLuint vao_=0;
    bool glReady_=false;
    bool replayingInput_=false;
    QTimer timer_;
    QElapsedTimer clock_;
    // Opt-in, bounded in-memory frame trace; written only on shutdown to avoid per-frame disk I/O.
    struct FrameSample { qint64 at,cpu; bool swap,rebuilt,moving; };
    QString frameTracePath_;
    QVector<FrameSample> frameTrace_;
    void saveFrameTrace();
    qint64 previousFrameNs_=0,reportAt_=0;
    quint64 frames_=0;
    double categoryPosition_=6;
    int previousCategory_=6;
    struct Column { QVector<Entry> entries; int selection=0; double position=0; PafEase ease; };
    PafEase categoryEase_,depthEase_,fadeEase_;
    QVector<Column> columns_;
    QVector<Column> outgoing_;
    int outgoingCategory_=6;
    double categoryFade_=0;
    // Time since the focused row (category, depth, selection) last changed: label glow phase and idle label fade.
    QElapsedTimer focusClock_;
    qint64 focusTimeOverride_=-1;
    qint64 focusTime() const { return focusTimeOverride_>=0?focusTimeOverride_:(focusClock_.isValid()?focusClock_.elapsed():0); }
    qint64 focusKey_=-1;
    double depthPosition_=0;
    int previousDepth_=0;
    void syncColumns();
    QImage mediaFrame_;
    PhotoViewport photoViewport_;
    MediaControls mediaControls_;
    bool previousMediaOpen_=false;
    QString previousMediaTitle_;
    void zoomPhoto(int direction);
    void resetPhotoView();
    void panPhoto(int dx,int dy);
    // GPU UI layer: 2x-sized 4x MSAA target, its resolve, and the premultiplied composite pass.
    std::unique_ptr<QOpenGLFramebufferObject> uiMsaa_,uiResolved_;
    QOpenGLShaderProgram uiComposite_;
    QString uiSignature_; // what the layer currently shows; redrawn when it changes
    TextRenderer textRenderer_;
    int uiRevision_=0;    // bumped on every shell change
    QImage wallpaperImage_,monthImage_;
    QHash<int,QVector3D> monthIconTints_;
    QString wallpaperPath_;
    quint64 wallpaperRequest_=0;
    std::unique_ptr<QOpenGLTexture> monthTexture_;
    void loadWallpaper();
    struct IconMaterial { QImage image; QRect bounds; };
    QHash<QString,IconMaterial> iconMaterials_;
    QHash<QString,QRect> imageBounds_; // visible (alpha) bounds per RCO image
    quint16 buttons_=0;
    qint64 repeatAt_=0;
    // Arrow keys held / pressed since the last poll, as D-pad bits: they repeat on the controller's firmware timing.
    quint16 keyHeld_=0,keyPressed_=0;
    void pollController();
    void renderBackground(float seconds,float light,int pixelWidth,int pixelHeight);
    // Original firmware background: qgl/lines back_colours0.fpo with this/next month's day and night palettes.
    struct BackgroundTime { float day=0,month=0,night=0,nightBrightness=0,nightDayBlend=1,alpha=1; };
    static constexpr double kNightBlend=.499947;
    static BackgroundTime backgroundTime(const QDateTime& when);
    void initOriginalBackground();
    bool loadMonthTextures(int month);
    int texturesMonth_=0;
    void renderOriginalBackground(const BackgroundTime& time,int pixelWidth,int pixelHeight);
    QOpenGLShaderProgram original_;
    bool originalReady_=false;
    CgProgram backColours_;
    std::array<std::unique_ptr<QOpenGLTexture>,4> monthTextures_; // tex0..3: day this/next month, night this/next month
    Ribbon ribbon_;
    Hdr hdr_;
    Particles particles_;
    Coldboot coldboot_;
    bool introEnabled_=true;
    // Boot state of the background (RPCS3 capture of back_colours0 through a cold boot): _Alpha fades in as smoothstep
    // over 7.5 s from BootBG2; NIGHT WHIT BIAS goes from coldboot1 to coldboot2 on the boot weights.
    float backgroundAlpha_=1,backgroundToColdboot2_=1,backgroundToClock_=1;
    // The intro is on screen (logo, no UI); any key or button skips it.
    bool introRunning() const { return coldboot_.ready() && coldboot_.seconds()<Coldboot::showGui; }
    IconRenderer icons_;
    QVector<IconRenderer::Icon> iconQueue_; // category icons for the firmware renderer, rebuilt with the UI layer
    float presetBlend_=1;
    static constexpr double kColumnX=565; // focus column, 1080p px (firmware: camera 2700, eye x -5)
    // Firmware quad size (1080p px) of an icontex item icon in a column (RPCS3 capture): level 0 77.616, focused 184.8;
    // settings bullets inside a page from the page capture.
    static double itemIconSize(int level,bool bullet,double focus);
    bool forceOriginal_=false;
    QDateTime backgroundOverride_;                                    // fixed clock for measurement, invalid = now
    double secondsOverride_=-1;                                       // fixed animation time for A/B captures, -1 = now
    void drawScene(QPainter& painter);
    void grabIntroFrame();
    // PS3 on-screen keyboard (reference frame 64 s): 10x4 character grid, two function rows, prediction panel.
    struct Osk { bool open=false,shift=false,symbols=false; QString title,text; int row=0,column=0,cursor=0; std::function<void(QString)> done; };
    Osk osk_;
    void openOsk(const QString& title,const QString& initial,std::function<void(QString)> done);
    void drawOsk(QPainter& p);
    void oskMove(int dx,int dy);
    void oskPress();                 // the focused key
    void oskInsert(const QString& value);
    void oskBackspace();
    void oskClose(bool accept);
    QString oskCharacter(int row,int column) const;
    void drawImage(QPainter& painter,const QString& container,const QString& texture,const QRectF& rect,qreal opacity=1);
    void drawCategory(QPainter& painter,const Category& category,const QRectF& rect);
};
}
