#include "text_renderer.h"
#include <QGlyphRun>
#include <QTextLayout>
#include <QTextOption>
#include <QVector2D>
#include <QDebug>
#include <QFontMetricsF>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <limits>

namespace xmb {
namespace {
// The reference's ~5px focused halo is still a measured approximation. Keep it
// separate from coverage so neither gamma nor the glyph edge is altered by glow.
constexpr qreal haloSigma=2.2,haloRadius=6;
QImage tileFor(const QImage& coverage,int pad,qreal sigma) {
    QImage tile(coverage.width()+2*pad,coverage.height()+2*pad,QImage::Format_RGBA8888);
    tile.fill(Qt::transparent);
    const int w=tile.width(),h=tile.height(),radius=std::max(1,int(std::ceil(3*sigma)));
    QVector<float> mask(w*h),horizontal(w*h),kernel(2*radius+1);float sum=0;
    for(int i=-radius;i<=radius;++i){const float k=float(std::exp(-i*i/(2*sigma*sigma)));kernel[i+radius]=k;sum+=k;}
    for(auto& k:kernel)k/=sum;
    for(int y=0;y<coverage.height();++y)for(int x=0;x<coverage.width();++x)
        mask[(y+pad)*w+x+pad]=coverage.pixelColor(x,y).redF();
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float value=0;for(int k=-radius;k<=radius;++k)if(x+k>=0 && x+k<w)value+=mask[y*w+x+k]*kernel[k+radius];
        horizontal[y*w+x]=value;
    }
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float value=0;for(int k=-radius;k<=radius;++k)if(y+k>=0 && y+k<h)value+=horizontal[(y+k)*w+x]*kernel[k+radius];
        auto* pixel=tile.scanLine(y)+4*x;pixel[0]=uchar(std::clamp(std::lround(mask[y*w+x]*255),0L,255L));
        pixel[1]=uchar(std::clamp(std::lround(value*255),0L,255L));pixel[2]=0;pixel[3]=255;
    }
    return tile;
}
// PAF's coverage copy: FUN_004057a0 @ 0x4057a0 stores an incoming coverage byte only when it is
// larger than the stored one; FUN_004056d8 @ 0x4056d8 does the same for the coloured variant. Never
// an add, never an alpha-over (audit/TEXT-RASTER.md 1.4-1.5). Pixels outside `into` are dropped;
// the caller sizes the canvas.
void unionCoverage(QImage& into,const QImage& add,int dx,int dy) {
    if(into.isNull() || add.isNull() || into.format()!=QImage::Format_Grayscale8
        || add.format()!=QImage::Format_Grayscale8)
        return;
    for(int y=0;y<add.height();++y) {
        const int ty=y+dy;
        if(ty<0 || ty>=into.height())continue;
        const uchar* source=add.constScanLine(y);
        uchar* destination=into.scanLine(ty);
        for(int x=0;x<add.width();++x) {
            const int tx=x+dx;
            if(tx<0 || tx>=into.width())continue;
            if(destination[tx]<source[x])destination[tx]=source[x];
        }
    }
}
}

bool TextRenderer::init(QOpenGLFunctions_3_3_Core& gl,const QString& fontDirectory) {
    useFreeType_=raster_.configure(fontDirectory) && !qEnvironmentVariableIntValue("XMB_TEXT_QT");
    // Per-phase coverage tiles are on by default: two phases per layer pixel on each axis, i.e. four
    // positions per output pixel. XMB_TEXT_PHASE=0 restores whole-output-pixel snapping.
    const bool phasesEnabled=qEnvironmentVariableIsSet("XMB_TEXT_PHASE")
        ?qEnvironmentVariableIntValue("XMB_TEXT_PHASE")!=0:true;
    phases_=useFreeType_ && phasesEnabled;
    const char* vertex=R"(#version 330 core
layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;layout(location=2) in vec4 bounds;
uniform vec2 viewport;out vec2 uv;flat out vec4 tileBounds;
void main(){uv=texcoord;tileBounds=bounds;gl_Position=vec4(position.x*2./viewport.x-1.,1.-position.y*2./viewport.y,0.,1.);}
)";
    const char* fragment=R"(#version 330 core
in vec2 uv;flat in vec4 tileBounds;out vec4 result;uniform sampler2D atlas;uniform vec4 tint;
uniform vec2 shadowOffset;uniform float glowStrength,shadowAlpha;uniform bool ink,shadowPass;uniform vec3 shadowColor;
void main(){
    vec2 coverage=texture(atlas,uv).rg;
    vec2 shadowUv=uv-shadowOffset;
    float inside=step(tileBounds.x,shadowUv.x)*step(tileBounds.y,shadowUv.y)*step(shadowUv.x,tileBounds.z)*step(shadowUv.y,tileBounds.w);
    float shadow=texture(atlas,shadowUv).r*inside*shadowAlpha*tint.a;
    if(shadowPass){result=vec4(shadowColor*shadow,shadow);return;}
    float halo=clamp(coverage.g*glowStrength,0.,1.)*tint.a;
    vec3 rgb=vec3(.922,.961,1.)*halo;
    float fill=ink?coverage.r*tint.a:0.;
    result=vec4(tint.rgb*fill+rgb*(1.-fill),fill+halo*(1.-fill));
}
)";
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) ||
       !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !program_.link()) {
        qWarning().noquote()<<"text atlas:"<<program_.log();return false;
    }
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    gl.glEnableVertexAttribArray(1);gl.glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(2*sizeof(float)));
    gl.glEnableVertexAttribArray(2);gl.glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(4*sizeof(float)));
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);ready_=true;
    qInfo()<<"text atlas:"<<(useFreeType_?raster_.version()+"; firmware flags 10 (no hinting/no bitmap)":QString("Qt coverage fallback; vertical hinting"))
           <<"grayscale coverage; measured Gaussian halo;"
           <<(phases_?"per-phase tiles (XMB_TEXT_PHASE)":"whole-output-pixel glyph origins");
    return true;
}
void TextRenderer::release(QOpenGLFunctions_3_3_Core& gl) {
    for(const auto& page:pages_)gl.glDeleteTextures(1,&page.texture);
    pages_.clear();faces_.clear();layouts_.clear();
    if(vbo_)gl.glDeleteBuffers(1,&vbo_);if(vao_)gl.glDeleteVertexArrays(1,&vao_);
    vbo_=vao_=0;program_.removeAllShaders();ready_=false;
}
void TextRenderer::beginFrame(QOpenGLFunctions_3_3_Core& gl,const QSize& target,qreal rasterScale,int pixelGrid) {
    target_=target;pixelGrid_=std::max(1,pixelGrid);
    rasterScale=std::max(.1,rasterScale);
    // Clear on resize/scale or a full atlas, at a frame boundary rather than
    // invalidating texture coordinates halfway through a shaped string.
    if(!qFuzzyCompare(rasterScale_,rasterScale) || pages_.size()>=maxPages) {
        for(const auto& page:pages_)gl.glDeleteTextures(1,&page.texture);
        pages_.clear();faces_.clear();layouts_.clear();rasterScale_=rasterScale;
    }
}
int TextRenderer::face(const QRawFont& font) {
    for(int i=0;i<int(faces_.size());++i)if(faces_[size_t(i)].font==font)return i;
    faces_.push_back({font,{}});
    const auto source=raster_.sourceFor(font);
    qInfo()<<"text face:"<<font.familyName()<<font.styleName()<<font.pixelSize()<<"source"<<source;
    if(useFreeType_ && source.isEmpty())qWarning()<<"text: unmatched font uses Qt coverage"<<font.familyName()<<font.styleName();
    return int(faces_.size())-1;
}
const TextRenderer::Glyph& TextRenderer::glyph(QOpenGLFunctions_3_3_Core& gl,int faceIndex,quint32 index,int phaseX,int phaseY) {
    auto& f=faces_[size_t(faceIndex)];
    // The phase is part of a glyph's identity: the same outline at a different pen phase rasterises
    // to different coverage, so it gets its own tile. Two phases per layer pixel per axis keeps the
    // atlas bounded (4x glyph tiles at worst) and every tile is deterministic for a fixed draw order.
    const quint64 key=(quint64(index)<<4)|(quint64(phaseY&3)<<2)|quint64(phaseX&3);
    const auto found=f.glyphs.constFind(key);
    if(found!=f.glyphs.cend())return *found;
    Glyph value;value.phaseX=phaseX;value.phaseY=phaseY;
    // libfontFT.prx cellFontFT_65E27CB3 @ 0x6aa4 loads outlines with flags 10, then uses
    // FT_Outline_Get_Bitmap. FT_RENDER_MODE_NORMAL uses FreeType's same grayscale outline rasterizer.
    // The delta is the masked 26.6 pen phase that the same function feeds FT_Set_Transform.
    const int step=qRound(64.0/phaseSteps);
    const QPoint phaseDelta(phaseX*step,phaseY*step);
    const auto raster=useFreeType_?raster_.rasterize(f.font,index,10,phaseDelta):FontRaster::RasterGlyph{};
    if(useFreeType_ && !raster.valid)qWarning()<<"text: Qt glyph coverage fallback"<<f.font.familyName()<<index;
    const auto raw=raster.valid?raster.coverage:f.font.alphaMapForGlyph(index,QRawFont::PixelAntialiasing);
    if(raw.isNull())return f.glyphs.insert(key,value).value(); // spaces retain their shaped advance
    const int pad=std::max(2,int(std::ceil(haloRadius*rasterScale_))+2);
    QImage coverage(raw.size(),QImage::Format_Grayscale8);
    for(int y=0;y<raw.height();++y)for(int x=0;x<raw.width();++x)
        coverage.scanLine(y)[x]=raw.format()==QImage::Format_Alpha8 || raw.format()==QImage::Format_Grayscale8
            ?raw.constScanLine(y)[x]:uchar(raw.hasAlphaChannel()?raw.pixelColor(x,y).alpha():raw.pixelColor(x,y).red());
    const auto tile=tileFor(coverage,pad,std::max(.5,haloSigma*rasterScale_));
    if(tile.width()>atlasSize || tile.height()>atlasSize)return f.glyphs.insert(key,value).value();
    // QPainter's image engine may leave sub-rectangle unpack state behind. Reset
    // it before allocation as well as subimage uploads; otherwise glyph spans vanish.
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    if(!pages_.empty()) {
        auto& p=pages_.back();if(p.x+tile.width()>atlasSize){p.x=0;p.y+=p.rowHeight+1;p.rowHeight=0;}
    }
    if(pages_.empty() || pages_.back().y+tile.height()>atlasSize) {
        // A phased tile is an enhancement: when the atlas is full, fall back to the phase-0 tile
        // rather than dropping the glyph from the string. Draw order is deterministic, so the
        // capture is reproducible either way.
        if(pages_.size()>=maxPages) {
            if(phaseX || phaseY) {
                static bool warned=false;
                if(!warned){warned=true;qWarning()<<"text atlas: phased tile fell back to phase 0; the atlas pages are full";}
                // Remember the phase-0 tile under the phased key too, so a full atlas does not force
                // a FreeType rasterisation and halo build on every frame for every glyph.
                const Glyph zero=glyph(gl,faceIndex,index,0,0);
                return f.glyphs.insert(key,zero).value();
            }
            return f.glyphs.insert(key,value).value();
        }
        Page p;gl.glGenTextures(1,&p.texture);gl.glBindTexture(GL_TEXTURE_2D,p.texture);
        // Empty areas and gutters must be zero for bilinear sampling at tile edges.
        QImage empty(atlasSize,atlasSize,QImage::Format_RGBA8888);empty.fill(Qt::transparent);
        gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,atlasSize,atlasSize,0,GL_RGBA,GL_UNSIGNED_BYTE,empty.constBits());
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        pages_.push_back(p);
    }
    auto& p=pages_.back();value.page=int(pages_.size())-1;value.atlas={p.x,p.y,tile.width(),tile.height()};
    const auto bounds=f.font.boundingRect(index);
    value.bearing=raster.valid?raster.bearing-QPoint(pad,pad):
        QPoint(int(std::floor(bounds.left()))-pad,int(std::floor(bounds.top()))-pad);
    gl.glBindTexture(GL_TEXTURE_2D,p.texture);gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    gl.glTexSubImage2D(GL_TEXTURE_2D,0,p.x,p.y,tile.width(),tile.height(),GL_RGBA,GL_UNSIGNED_BYTE,tile.constBits());
    p.x+=tile.width()+1;p.rowHeight=std::max(p.rowHeight,tile.height());
    return f.glyphs.insert(key,value).value();
}
TextRenderer::Layout& TextRenderer::layout(const QString& text,const QFont& font,qreal width,int flags) {
    const QString display=FontRaster::mapSymbols(text);
    const QString key=font.key()+QChar(0x1f)+QString::number(width,'f',3)+':'+QString::number(flags)+QChar(0x1f)+display;
    auto found=layouts_.find(key);if(found!=layouts_.end()){found->used=++serial_;return *found;}
    if(layouts_.size()>=maxLayouts) {
        auto oldest=layouts_.begin();for(auto it=layouts_.begin();it!=layouts_.end();++it)if(it->used<oldest->used)oldest=it;
        layouts_.erase(oldest);
    }
    Layout value;value.used=++serial_;
    // QTextLayout shapes complex scripts and chooses registered firmware fallback
    // faces. Do not index UTF-16 code units as if each were a glyph.
    const auto paragraphs=display.split('\n');
    for(const auto& paragraph:paragraphs) {
        QTextLayout shaped(paragraph,font);QTextOption option;
        option.setWrapMode(flags&Qt::TextWordWrap?QTextOption::WrapAtWordBoundaryOrAnywhere:QTextOption::NoWrap);
        option.setAlignment(Qt::Alignment(flags&Qt::AlignHorizontal_Mask));shaped.setTextOption(option);
        shaped.beginLayout();bool any=false;
        while(true) {
            auto line=shaped.createLine();if(!line.isValid())break;
            line.setLineWidth(std::max(1.,width));line.setPosition({0,value.height});value.height+=line.height();any=true;
        }
        shaped.endLayout();
        if(!any)value.height+=QFontMetricsF(font).height();
        for(const auto& run:shaped.glyphRuns()) {
            const int f=face(run.rawFont());const auto indices=run.glyphIndexes();const auto positions=run.positions();
            for(int i=0;i<indices.size();++i)value.glyphs.push_back({f,indices[i],positions[i]});
        }
    }
    return layouts_.insert(key,std::move(value)).value();
}
void TextRenderer::draw(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
                        const QString& text,const QFont& requested,const QColor& colour,int flags,
                        qreal glow,bool shadow,bool ink) {
    if(!ready_ || text.isEmpty() || colour.alpha()==0 || painter.opacity()<=0)return;
    QFont font=requested;
    if(font.styleName().isEmpty() && font.weight()==QFont::Normal && !font.italic())font.setStyleName("Regular");
    font.setPixelSize(std::max(1,int(std::lround(requested.pixelSize()*rasterScale_))));
    font.setStyleStrategy(QFont::PreferAntialias);
    // The firmware never hints: libfontFT loads glyphs with FT_LOAD_NO_HINTING (audit/TEXT-RASTER.md), so both
    // rasteriser paths must stay unhinted or stems snap differently from the reference.
    font.setHintingPreference(QFont::PreferNoHinting);
    auto& shaped=layout(text,font,rect.width()*rasterScale_,flags);
    qreal offset=0;if(flags&Qt::AlignVCenter)offset=(rect.height()*rasterScale_-shaped.height)/2;
    else if(flags&Qt::AlignBottom)offset=rect.height()*rasterScale_-shaped.height;
    const auto transform=painter.combinedTransform();
    const qreal opacity=painter.opacity()*colour.alphaF();
    const bool clipped=painter.hasClipping();const QRect clip=transform.mapRect(painter.clipBoundingRect()).toAlignedRect().intersected(QRect(QPoint(),target_));
    painter.beginNativePainting();
    gl.glViewport(0,0,target_.width(),target_.height());
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_STENCIL_TEST);gl.glDisable(GL_CULL_FACE);
    if(clipped){gl.glEnable(GL_SCISSOR_TEST);gl.glScissor(clip.x(),target_.height()-clip.bottom()-1,clip.width(),clip.height());}
    else gl.glDisable(GL_SCISSOR_TEST);
    gl.glEnable(GL_BLEND);gl.glBlendEquation(GL_FUNC_ADD);gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindSampler(0,0);program_.bind();
    program_.setUniformValue("viewport",QVector2D(target_.width(),target_.height()));program_.setUniformValue("atlas",0);
    program_.setUniformValue("tint",QVector4D(colour.redF(),colour.greenF(),colour.blueF(),opacity));
    program_.setUniformValue("glowStrength",float(std::clamp(glow,0.,1.)*1.5));
    // RCO template offsets (4,-5) are not measured screen pixels. Applying them directly made a dark duplicate
    // behind every label. Keep a restrained one-scene-pixel shadow pending recovery of the runtime conversion.
    program_.setUniformValue("shadowAlpha",.2f);
    program_.setUniformValue("shadowColor",QVector3D(0,0,0));
    program_.setUniformValue("ink",ink);
    program_.setUniformValue("shadowOffset",QVector2D(float(rasterScale_/atlasSize),float(rasterScale_/atlasSize)));
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    struct Batch {int page,first,count;};
    QVector<Vertex> vertices;QVector<Batch> batches;
    // Glyph origins are resolved in layer pixels. The pen's fraction of a layer pixel selects a
    // coverage tile that was rasterised at that phase, so a glyph no longer has to land on a whole
    // output pixel. The UI painter transform is a uniform scale plus translation (view.cpp), so a
    // glyph quad stays axis-aligned in layer space.
    const QPointF layerOrigin=transform.map(QPointF(0,0));
    const QPointF layerUnitX=transform.map(QPointF(1,0)),layerUnitY=transform.map(QPointF(0,1));
    const qreal scaleX=layerUnitX.x()-layerOrigin.x(),scaleY=layerUnitY.y()-layerOrigin.y();
    // Batch consecutive glyphs on the same page without changing shaped order.
    for(const auto& placed:shaped.glyphs) {
        const qreal penX=transform.map(QPointF(rect.left()+placed.position.x()/rasterScale_,0)).x();
        const qreal penY=transform.map(QPointF(0,rect.top()+(placed.position.y()+offset)/rasterScale_)).y();
        const auto& g=glyph(gl,placed.face,placed.index,
                            phases_?penPhase(penX):0,phases_?penPhase(penY):0);if(g.page<0)continue;
        // The unphased origin is the same layer-pixel tile origin this renderer used before per-phase
        // tiles existed: the whole tile is snapped to the output-pixel grid, which is what the
        // firmware's ((phase+pen)+0x3f)>>6 blit origin does. A phase-shifted tile instead sits on the
        // whole-pixel part of the pen, because its bearing already carries the phase.
        const bool phased=phases_ && (g.phaseX || g.phaseY);
        qreal originX=0,originY=0;
        if(phased) {
            originX=std::floor(penX)+scaleX*g.bearing.x()/rasterScale_;
            originY=std::floor(penY)+scaleY*g.bearing.y()/rasterScale_;
        } else {
            const QPointF at=rect.topLeft()+QPointF((placed.position.x()+g.bearing.x())/rasterScale_,
                                                    (placed.position.y()+offset+g.bearing.y())/rasterScale_);
            const QPointF origin=transform.map(at);
            originX=std::round(origin.x()/pixelGrid_)*pixelGrid_;
            originY=std::round(origin.y()/pixelGrid_)*pixelGrid_;
        }
        const qreal width=scaleX*g.atlas.width()/rasterScale_,height=scaleY*g.atlas.height()/rasterScale_;
        const QPointF tl(originX,originY),tr(originX+width,originY),bl(originX,originY+height),br(originX+width,originY+height);
        const float u=float(g.atlas.x())/atlasSize,v=float(g.atlas.y())/atlasSize,r=float(g.atlas.x()+g.atlas.width())/atlasSize,b=float(g.atlas.y()+g.atlas.height())/atlasSize;
        if(batches.isEmpty() || batches.back().page!=g.page)batches.push_back({g.page,int(vertices.size()),0});
        const auto vertex=[&](const QPointF& p,float s,float t){return Vertex{float(p.x()),float(p.y()),s,t,u,v,r,b};};
        vertices<<vertex(tl,u,v)<<vertex(bl,u,b)<<vertex(tr,r,v)<<vertex(tr,r,v)<<vertex(bl,u,b)<<vertex(br,r,b);
        batches.back().count+=6;
    }
    gl.glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.constData(),GL_STREAM_DRAW);
    const auto drawPass=[&](bool isShadow){
        program_.setUniformValue("shadowPass",isShadow);
        for(const auto& batch:batches){
            gl.glBindTexture(GL_TEXTURE_2D,pages_[size_t(batch.page)].texture);
            gl.glDrawArrays(GL_TRIANGLES,batch.first,batch.count);
        }
    };
    // Draw the whole string's shadow first, including across atlas pages. A later glyph's shadow must never
    // cover an earlier glyph's foreground (especially with kerning, ligatures and combining marks).
    if(shadow)drawPass(true);
    drawPass(false);
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);gl.glBindTexture(GL_TEXTURE_2D,0);program_.release();
    painter.endNativePainting();
    if(!checked_ && qEnvironmentVariableIntValue("XMB_TEXT_CHECK")){checked_=true;selfCheck();}
}

int TextRenderer::penPhase(qreal penX) {
    // The firmware's pen is a 26.6 position; the phase it masks with 0x3f is that fraction of a
    // pixel. Two phases per layer pixel are what the 2x supersampled layer can express, so the
    // residual after quantisation is at most 1/4 layer pixel (1/8 output pixel).
    const qreal fraction=penX-std::floor(penX);
    return std::clamp(int(std::lround(fraction*phaseSteps)),0,phaseSteps-1);
}

qreal TextRenderer::measure(const QFont& font,const QString& text) const {
    // The drawn width of one line, measured by shaping exactly the way draw() shapes: same pixel
    // size at layer scale, same hinting preference, same fallback faces. Returned in scene units.
    if(text.isEmpty())return 0;
    const qreal scale=std::max(1.,rasterScale_);
    QFont shaped=font;
    if(shaped.styleName().isEmpty() && shaped.weight()==QFont::Normal && !shaped.italic())shaped.setStyleName("Regular");
    shaped.setPixelSize(std::max(1,int(std::lround(font.pixelSize()*scale))));
    shaped.setStyleStrategy(QFont::PreferAntialias);
    shaped.setHintingPreference(QFont::PreferNoHinting);
    qreal width=0;
    for(const auto& paragraph:FontRaster::mapSymbols(text).split('\n')) {
        QTextLayout lay(paragraph,shaped);
        QTextOption option;option.setWrapMode(QTextOption::NoWrap);lay.setTextOption(option);
        lay.beginLayout();
        auto line=lay.createLine();
        if(line.isValid()){line.setLineWidth(1e9);line.setPosition({0,0});}
        lay.endLayout();
        if(line.isValid())width=std::max(width,line.naturalTextWidth());
    }
    return width/scale;
}

TextRenderer::Elision TextRenderer::elideToWidth(const QFont& font,const QString& text,qreal maxWidth) const {
    // Firmware shape (audit/TEXT-RASTER.md 4.2): explore_plugin measures the label, finds the last
    // visible character, and puts a separate label2_text_ellipsis Text object there. PAF itself
    // only draws a character range, so this returns the prefix and the pen x; the caller draws the
    // ellipsis as its own text, never as a substituted U+2026 inside the string.
    Elision result;result.prefix=text;
    if(text.isEmpty() || maxWidth<=0)return result;
    const qreal whole=measure(font,text);
    if(whole<=maxWidth){result.width=whole;result.ellipsisX=whole;return result;}
    result.elided=true;
    const qreal limit=maxWidth-measure(font,QString(QChar(0x2026)));
    int best=0;qreal bestWidth=0;
    for(int i=1;i<=text.size();++i) {
        const qreal width=measure(font,text.left(i));
        if(width>limit)break;
        best=i;bestWidth=width;
    }
    // Keep the boundary on a whole cluster: never split a surrogate pair, and never cut a base
    // letter away from a combining mark that follows it.
    while(best>0 && (text.at(best-1).isHighSurrogate() ||
                     (best<text.size() && text.at(best).isMark()))) {
        --best;bestWidth=best?measure(font,text.left(best)):0;
    }
    result.prefix=text.left(best);result.width=bestWidth;result.ellipsisX=bestWidth;
    return result;
}

void TextRenderer::selfCheck() {
    // Deterministic assertions for the two renderer-level rules that can be checked without a
    // capture: the elision width rule (4.2) and the keep-larger coverage union (1.5). Run with
    // XMB_TEXT_CHECK=1; every line is logged, so a scripted run is the evidence.
    if(faces_.empty()){qInfo().noquote()<<"text check: no shaped face yet; skipped";return;}
    const auto& raw=faces_.front().font;
    QFont font(raw.familyName());
    if(!raw.styleName().isEmpty())font.setStyleName(raw.styleName());
    font.setPixelSize(27);
    bool pass=true;
    const auto report=[&](const char* name,bool ok,const QString& detail) {
        pass&=ok;qInfo().noquote()<<QString("text check: %1 %2 %3").arg(ok?"PASS":"FAIL",name,detail);
    };
    // Elision must leave a fitting string untouched and, when it does elide, leave the prefix plus
    // the reserved ellipsis inside the limit.
    const QString longText=QStringLiteral("Corrupted Data");
    const auto fits=elideToWidth(font,longText,1100);
    report("elision-fits",!fits.elided && fits.prefix==longText && fits.width<=1100,
           QString("prefix=%1 width=%2 elided=%3").arg(fits.prefix).arg(fits.width,0,'f',2).arg(fits.elided));
    const auto cut=elideToWidth(font,longText,100);
    const qreal ellipsis=measure(font,QString(QChar(0x2026)));
    report("elision-width",cut.elided && cut.prefix.size()<longText.size()
               && longText.startsWith(cut.prefix) && cut.width+ellipsis<=100
               && (cut.prefix.size()==longText.size() || measure(font,longText.left(cut.prefix.size()+1))+ellipsis>100),
           QString("prefix=\"%1\" width=%2 + ellipsis %3 <= 100").arg(cut.prefix).arg(cut.width,0,'f',2).arg(ellipsis,0,'f',2));
    const auto empty=elideToWidth(font,QString(),100);
    report("elision-empty",!empty.elided && empty.prefix.isEmpty() && empty.width==0,"empty=0");
    // Metric-source evidence: PAF sums unhinted 26.6 advances (_opd_FUN_00397e5c @ 0x397e5c). Compare
    // that sum with the width the renderer actually shapes *at the same pixel size*, so any
    // difference is a metric difference and not a size difference.
    const QString clock=QStringLiteral("25/10  12:32");
    QFont same=font;
    same.setPixelSize(std::max(1,int(std::lround(raw.pixelSize()/std::max(1.,rasterScale_)))));
    qreal paf=0;
    for(const quint32 glyph:raw.glyphIndexesForString(clock))paf+=raster_.advance(raw,glyph);
    const qreal shaped=measure(same,clock);
    report("metric-authority",std::abs(paf-shaped)<=1.0,
           QString("PAF advance %1 px vs Qt shaped %2 px at %3 px font").arg(paf,0,'f',2).arg(shaped,0,'f',2)
               .arg(raw.pixelSize()));
    // Keep-larger union versus the alpha-over this renderer's per-glyph quads actually produce.
    // e + U+0301 at one pen first: if the accent's ink does not touch the base there is nothing for
    // the union rule to change in an accented pair.
    const auto baseGlyphs=raw.glyphIndexesForString(QStringLiteral("e"));
    const auto markGlyphs=raw.glyphIndexesForString(QString(QChar(0x301)));
    const auto base=baseGlyphs.isEmpty()?FontRaster::RasterGlyph{}:raster_.rasterize(raw,baseGlyphs.front(),10);
    const auto mark=markGlyphs.isEmpty()?FontRaster::RasterGlyph{}:raster_.rasterize(raw,markGlyphs.front(),10);
    int accentOverlap=0;
    if(base.valid && mark.valid) {
        const QRect baseBox(base.bearing,QSize(base.coverage.size()));
        const QRect markBox(mark.bearing,QSize(mark.coverage.size()));
        for(int y=0;y<mark.coverage.height();++y)for(int x=0;x<mark.coverage.width();++x) {
            const int bx=x+markBox.x()-baseBox.x(),by=y+markBox.y()-baseBox.y();
            if(bx<0 || by<0 || bx>=base.coverage.width() || by>=base.coverage.height())continue;
            if(base.coverage.constScanLine(by)[bx] && mark.coverage.constScanLine(y)[x])++accentOverlap;
        }
    }
    qInfo().noquote()<<QString("text check: accent pair e+U+0301 overlap %1 px at one pen").arg(accentOverlap);
    // The rule itself needs masks that overlap: shift a copy of the same glyph one pixel right, which
    // is what a tight kerned pair or a combining mark drawn back over its base produces.
    if(base.valid && base.coverage.width()>1) {
        QImage unioned=base.coverage.convertToFormat(QImage::Format_Grayscale8);
        unionCoverage(unioned,base.coverage,1,0);
        int overlap=0,strict=0;
        for(int y=0;y<base.coverage.height();++y)for(int x=1;x<base.coverage.width();++x) {
            const int a=base.coverage.constScanLine(y)[x],b=base.coverage.constScanLine(y)[x-1];
            if(!a || !b)continue;
            ++overlap;
            const int over=255-((255-a)*(255-b)+127)/255; // what alpha-over produces today
            if(unioned.constScanLine(y)[x]<over)++strict;
        }
        report("union-rule",overlap>0 && strict>0,
               QString("overlapping coverage %1 px, keep-larger below alpha-over at %2 px").arg(overlap).arg(strict));
    } else report("union-rule",false,"no coverage for e");
    qInfo().noquote()<<QString("text check: %1").arg(pass?"all assertions passed":"assertion failure");
}
}
