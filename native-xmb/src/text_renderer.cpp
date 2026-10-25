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
}

bool TextRenderer::init(QOpenGLFunctions_3_3_Core& gl) {
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
    qInfo()<<"text atlas: grayscale native-resolution coverage; vertical hinting; measured Gaussian halo";
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
    faces_.push_back({font,{}});return int(faces_.size())-1;
}
const TextRenderer::Glyph& TextRenderer::glyph(QOpenGLFunctions_3_3_Core& gl,int faceIndex,quint32 index) {
    auto& f=faces_[size_t(faceIndex)];const auto found=f.glyphs.constFind(index);
    if(found!=f.glyphs.cend())return *found;
    Glyph value;
    const auto raw=f.font.alphaMapForGlyph(index,QRawFont::PixelAntialiasing);
    if(raw.isNull())return f.glyphs.insert(index,value).value(); // spaces retain their shaped advance
    const int pad=std::max(2,int(std::ceil(haloRadius*rasterScale_))+2);
    QImage coverage(raw.size(),QImage::Format_Grayscale8);
    for(int y=0;y<raw.height();++y)for(int x=0;x<raw.width();++x)
        coverage.scanLine(y)[x]=raw.format()==QImage::Format_Alpha8 || raw.format()==QImage::Format_Grayscale8
            ?raw.constScanLine(y)[x]:uchar(raw.hasAlphaChannel()?raw.pixelColor(x,y).alpha():raw.pixelColor(x,y).red());
    const auto tile=tileFor(coverage,pad,std::max(.5,haloSigma*rasterScale_));
    if(tile.width()>atlasSize || tile.height()>atlasSize)return f.glyphs.insert(index,value).value();
    // QPainter's image engine may leave sub-rectangle unpack state behind. Reset
    // it before allocation as well as subimage uploads; otherwise glyph spans vanish.
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    if(!pages_.empty()) {
        auto& p=pages_.back();if(p.x+tile.width()>atlasSize){p.x=0;p.y+=p.rowHeight+1;p.rowHeight=0;}
    }
    if(pages_.empty() || pages_.back().y+tile.height()>atlasSize) {
        if(pages_.size()>=maxPages)return f.glyphs.insert(index,value).value();
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
    value.bearing={int(std::floor(bounds.left()))-pad,int(std::floor(bounds.top()))-pad};
    gl.glBindTexture(GL_TEXTURE_2D,p.texture);gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    gl.glTexSubImage2D(GL_TEXTURE_2D,0,p.x,p.y,tile.width(),tile.height(),GL_RGBA,GL_UNSIGNED_BYTE,tile.constBits());
    p.x+=tile.width()+1;p.rowHeight=std::max(p.rowHeight,tile.height());
    return f.glyphs.insert(index,value).value();
}
TextRenderer::Layout& TextRenderer::layout(const QString& text,const QFont& font,qreal width,int flags) {
    const QString key=font.key()+QChar(0x1f)+QString::number(width,'f',3)+':'+QString::number(flags)+QChar(0x1f)+text;
    auto found=layouts_.find(key);if(found!=layouts_.end()){found->used=++serial_;return *found;}
    if(layouts_.size()>=maxLayouts) {
        auto oldest=layouts_.begin();for(auto it=layouts_.begin();it!=layouts_.end();++it)if(it->used<oldest->used)oldest=it;
        layouts_.erase(oldest);
    }
    Layout value;value.used=++serial_;
    // QTextLayout shapes complex scripts and chooses registered firmware fallback
    // faces. Do not index UTF-16 code units as if each were a glyph.
    const auto paragraphs=text.split('\n');
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
    font.setPixelSize(std::max(1,int(std::lround(requested.pixelSize()*rasterScale_))));
    font.setStyleStrategy(QFont::PreferAntialias);font.setHintingPreference(QFont::PreferVerticalHinting);
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
    // Batch consecutive glyphs on the same page without changing shaped order.
    for(const auto& placed:shaped.glyphs) {
        const auto& g=glyph(gl,placed.face,placed.index);if(g.page<0)continue;
        const QPointF at=rect.topLeft()+QPointF((placed.position.x()+g.bearing.x())/rasterScale_,(placed.position.y()+offset+g.bearing.y())/rasterScale_);
        const QRectF box(at,QSizeF(g.atlas.size())/rasterScale_);
        // Each glyph's origin snaps to whole output pixels (pixelGrid_ layer pixels), so a one-pixel stem does not
        // straddle two output pixels after the 2x2 resolve.
        const QPointF origin=transform.map(box.topLeft());
        const QPointF snap(std::round(origin.x()/pixelGrid_)*pixelGrid_-origin.x(),std::round(origin.y()/pixelGrid_)*pixelGrid_-origin.y());
        const QPointF tl=origin+snap,tr=transform.map(box.topRight())+snap,bl=transform.map(box.bottomLeft())+snap,br=transform.map(box.bottomRight())+snap;
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
}
}
