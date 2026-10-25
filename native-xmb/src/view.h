#pragma once
#include "assets.h"
#include "hdr.h"
#include "particles.h"
#include "icons.h"
#include "ribbon.h"
#include "rsx_program.h"
#include "shell.h"
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
    // Every XMB move closes 25% of the remaining distance per 60 fps frame (reference video): (1/60)/ln(4/3) s.
    static constexpr double easeTau=0.05793;
    // Renders the background offscreen at reference times and writes the same statistics measured from the
    // reference videos (band colours by clock time, ribbon crest over 40 s). Returns false on GL failure.
    bool measureReference(const QString& path);
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    Shell& shell_;
    Assets& assets_;
    QOpenGLShaderProgram background_;
    GLuint vao_=0;
    bool glReady_=false;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 previous_=0,reportAt_=0;
    quint64 frames_=0;
    double categoryPosition_=6;
    int previousCategory_=6;
    struct Column { QVector<Entry> entries; int selection=0; double position=0; };
    QVector<Column> columns_;
    QVector<Column> outgoing_;
    int outgoingCategory_=6;
    double categoryFade_=0;
    double depthPosition_=0;
    int previousDepth_=0;
    void syncColumns();
    QImage mediaFrame_;
    // GPU UI layer: 2x-sized 4x MSAA target, its resolve, and the premultiplied composite pass.
    std::unique_ptr<QOpenGLFramebufferObject> uiMsaa_,uiResolved_;
    QOpenGLShaderProgram uiComposite_;
    QString uiSignature_; // what the layer currently shows; redrawn when it changes
    TextRenderer textRenderer_;
    int uiRevision_=0;    // bumped on every shell change
    QImage wallpaperImage_,monthImage_;
    QString wallpaperPath_;
    quint64 wallpaperRequest_=0;
    std::unique_ptr<QOpenGLTexture> monthTexture_;
    void loadWallpaper();
    struct IconMaterial { QImage image; QRect bounds; };
    QHash<QString,IconMaterial> iconMaterials_;
    QHash<QString,QRect> imageBounds_; // visible (alpha) bounds per RCO image
    quint16 buttons_=0;
    qint64 repeatAt_=0;
    void pollController();
    void renderBackground(float seconds,float light,int pixelWidth,int pixelHeight);
    // Original firmware background: qgl/lines back_colours0.fpo with this/next month's day and night palettes.
    struct BackgroundTime { float day=0,month=0,night=0,nightBrightness=0,nightDayBlend=1,alpha=1; };
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
    bool forceOriginal_=false;
    QDateTime backgroundOverride_;                                    // fixed clock for measurement, invalid = now
    IconRenderer icons_;
    QVector<IconRenderer::Icon> iconQueue_; // category icons for the firmware renderer, rebuilt with the UI layer
    float presetBlend_=1;
    static constexpr double kColumnX=565; // focus column, 1080p px (firmware: camera 2700, eye x -5)
    void drawScene(QPainter& painter);
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
