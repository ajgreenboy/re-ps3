#pragma once
#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QRawFont>
#include <memory>
#include <vector>
#include "font_raster.h"

namespace xmb {
// Shaping stays with QTextLayout (including fallback faces and bidi); glyph coverage is
// rasterised once at display resolution and uploaded to an atlas. Qt never paints the labels.
// Original faces use FreeType with the recovered firmware load flags. Qt still shapes text;
// this is not a complete port of PS3 libfont/PAF metrics and composition.
class TextRenderer {
public:
    bool init(QOpenGLFunctions_3_3_Core& gl,const QString& fontDirectory);
    void release(QOpenGLFunctions_3_3_Core& gl);
    // target: the layer's pixel size; rasterScale: layer pixels per 1920x1080 unit; pixelGrid: layer pixels per
    // output pixel (2 for the supersampled UI layer), the grid glyph origins snap to so stems stay on whole pixels.
    void beginFrame(QOpenGLFunctions_3_3_Core& gl,const QSize& target,qreal rasterScale,int pixelGrid=1);
    bool ready() const { return ready_; }
    void draw(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
              const QString& text,const QFont& font,const QColor& colour,int flags,
              qreal glow=0,bool shadow=true,bool ink=true);
private:
    struct Glyph { int page=-1; QRect atlas; QPoint bearing; };
    struct Face { QRawFont font; QHash<quint32,Glyph> glyphs; };
    struct Page { GLuint texture=0; int x=0,y=0,rowHeight=0; };
    struct PlacedGlyph { int face=0; quint32 index=0; QPointF position; };
    struct Layout { QVector<PlacedGlyph> glyphs; qreal height=0; quint64 used=0; };
    struct Vertex { float x,y,u,v,left,top,right,bottom; };
    int face(const QRawFont& font);
    const Glyph& glyph(QOpenGLFunctions_3_3_Core& gl,int face,quint32 index);
    Layout& layout(const QString& text,const QFont& font,qreal width,int flags);
    QOpenGLShaderProgram program_;
    FontRaster raster_;
    bool useFreeType_=false;
    GLuint vao_=0,vbo_=0;
    bool ready_=false;
    QSize target_;
    qreal rasterScale_=1;
    int pixelGrid_=1;
    quint64 serial_=0;
    std::vector<Face> faces_;
    std::vector<Page> pages_;
    QHash<QString,Layout> layouts_;
    static constexpr int atlasSize=1024,maxPages=16,maxLayouts=512;
};
}
