#pragma once
#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QRawFont>
#include <QVector>
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
    // PAF has no font-layer ellipsis: explore_plugin measures the label text object, finds the
    // position of the last visible character and positions a *separate* label2_text_ellipsis Text
    // object there (audit/TEXT-RASTER.md 4.2). This mirrors that shape instead of substituting
    // U+2026 inside the string with a platform metric engine: the widest whole-glyph prefix that
    // fits maxWidth once the ellipsis advance is reserved, plus the pen x the ellipsis draws at.
    struct Elision {
        QString prefix;      // text up to the last whole glyph that fits
        qreal width = 0;     // FreeType advance width of prefix, in pixels at font.pixelSize()
        qreal ellipsisX = 0; // pen x, relative to the string origin, where U+2026 is drawn
        bool elided = false;
    };
    bool init(QOpenGLFunctions_3_3_Core& gl,const QString& fontDirectory);
    void release(QOpenGLFunctions_3_3_Core& gl);
    // target: the layer's pixel size; rasterScale: layer pixels per 1920x1080 unit; pixelGrid: layer pixels per
    // output pixel (2 for the supersampled UI layer), the grid glyph origins snap to so stems stay on whole pixels.
    void beginFrame(QOpenGLFunctions_3_3_Core& gl,const QSize& target,qreal rasterScale,int pixelGrid=1);
    bool ready() const { return ready_; }
    void draw(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
              const QString& text,const QFont& font,const QColor& colour,int flags,
              qreal glow=0,bool shadow=true,bool ink=true);
    // Pure metric helpers: no GL state, no atlas page, no cache. They use the same unhinted
    // FreeType advances that place glyphs, so a caller that adopts them keeps one metric authority.
    Elision elideToWidth(const QFont& font,const QString& text,qreal maxWidth) const;
    qreal measure(const QFont& font,const QString& text) const;
    // Deterministic self-check for the elision rule, the keep-larger coverage union and the mask path.
    // Logs one line per assertion; enabled by XMB_TEXT_CHECK=1 so a scripted run records the result
    // (see audit/TEXT-RENDERER.md). The GL probe renders through the real mask pass and reads it back,
    // so it compares the shipped union path with the pure helper rather than re-testing the helper.
    void selfCheck(QOpenGLFunctions_3_3_Core& gl);
private:
    struct Glyph { int page=-1; QRect atlas; QPoint bearing; int phaseX=0,phaseY=0; QRect ink; };
    struct Face { QRawFont font; QHash<quint64,Glyph> glyphs; };
    struct Page { GLuint texture=0; int x=0,y=0,rowHeight=0; };
    struct PlacedGlyph { int face=0; quint32 index=0; QPointF position; };
    struct Layout { QVector<PlacedGlyph> glyphs; qreal height=0; quint64 used=0; };
    struct Vertex { float x,y,u,v,left,top,right,bottom; };
    struct Batch { int page=0,first=0,count=0; };
    int face(const QRawFont& font);
    // Phase bucket for a pen coordinate in layer pixels: the fraction quantised to 1/phaseSteps of a layer pixel.
    static int penPhase(qreal penX);
    const Glyph& glyph(QOpenGLFunctions_3_3_Core& gl,int face,quint32 index,int phaseX,int phaseY);
    Layout& layout(const QString& text,const QFont& font,qreal width,int flags);
    // Coverage-union target. PAF's copy rule is compare-and-keep-larger (FUN_004057a0 @ 0x4057a0),
    // so a string whose glyph inks overlap is drawn into a single R8 mask with GL_MAX and composited
    // once; strings without overlapping ink keep the per-glyph alpha-over fast path. The target is one
    // R8 texture the size of the UI layer, created on first use and only recreated when that size
    // changes, then cleared inside a scissor per string. GL_NEAREST sampling keeps the 1:1 layer map
    // exact and the result deterministic.
    bool ensureMask(QOpenGLFunctions_3_3_Core& gl);
    void maskPass(QOpenGLFunctions_3_3_Core& gl,const QVector<Batch>& batches,const QRect& box);
    QOpenGLShaderProgram program_;
    QOpenGLShaderProgram maskProgram_,compositeProgram_;
    FontRaster raster_;
    bool useFreeType_=false;
    GLuint vao_=0,vbo_=0;
    bool ready_=false;
    QSize target_;
    qreal rasterScale_=1;
    int pixelGrid_=1;
    quint64 serial_=0;
    bool checked_=false;
    GLuint maskTexture_=0,maskFbo_=0;
    QSize maskSize_;
    bool maskAvailable_=false;
    bool maskLogged_=false;
    // -1 auto (only overlapping ink), 0 off (XMB_TEXT_MASK=0), 1 forced for every ink/shadow string
    // (XMB_TEXT_MASK=1, for the A/B and frame-time measurement).
    int maskMode_=-1;
    // Reused per-draw scratch so a frame does not allocate a fresh vertex vector per string. These
    // grow to the widest string and are then reused; the mask target itself is the only large buffer.
    QVector<Vertex> vertexBuffer_;
    QVector<Batch> batchBuffer_;
    QVector<QRectF> boxBuffer_;
    // Sub-pixel pen phases are per-phase coverage tiles: the pen's fraction of a layer pixel on each
    // axis selects a tile rasterised at that phase. On by default when FreeType coverage is in use;
    // XMB_TEXT_PHASE=0 restores whole-output-pixel snapping to the 26.6 blit origin
    // ((phase+pen+0x3f)>>6), which was the earlier behaviour. Measured on the canonical capture:
    // clock text 13.51 -> 13.66 dB, info bar 17.12 -> 17.26 dB (audit/TEXT-RENDERER.md).
    bool phases_=true;
    std::vector<Face> faces_;
    std::vector<Page> pages_;
    QHash<QString,Layout> layouts_;
    static constexpr int atlasSize=1024,maxPages=16,maxLayouts=512;
    // Pen phases per layer pixel, per axis. The UI layer is 2x the window, so two phases per
    // layer pixel are four per output pixel. Each glyph tile is cached per (x,y) phase pair, so the
    // worst case is 4x the glyph tiles; the firmware's 26.6 pen has a 1/64 phase and this is the
    // quantisation the 2x2 resolve can actually express.
    static constexpr int phaseSteps=2;
};
}
