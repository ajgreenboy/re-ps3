#pragma once
#include "assets.h"
#include "shell.h"
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
    QImage wallpaperImage_,monthImage_;
    QString wallpaperPath_;
    quint64 wallpaperRequest_=0;
    std::unique_ptr<QOpenGLTexture> monthTexture_;
    void loadWallpaper();
    struct IconMaterial { QImage image; QRect bounds; };
    QHash<QString,IconMaterial> iconMaterials_;
    quint16 buttons_=0;
    qint64 repeatAt_=0;
    void pollController();
    void drawScene(QPainter& painter);
    void drawImage(QPainter& painter,const QString& container,const QString& texture,const QRectF& rect,qreal opacity=1);
    void drawCategory(QPainter& painter,const Category& category,const QRectF& rect);
};
}
