#include "text_renderer.h"
#include <QDir>
#include <QGlyphRun>
#include <QTextLayout>
#include <QTextOption>
#include <QVector2D>
#include <QDebug>
#include <QElapsedTimer>
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
// The mask path is only needed when two glyph inks can actually land on the same layer pixel. The
// per-glyph ink rectangles are tight (nonzero coverage only, halo padding excluded), so this is a
// conservative test of pixel overlap: rectangles that do not meet cannot share a covered pixel.
bool anyOverlap(const QVector<QRectF>& boxes) {
    for(int i=0;i<boxes.size();++i) {
        if(boxes[i].isEmpty())continue;
        for(int j=i+1;j<boxes.size();++j)
            if(!boxes[j].isEmpty() && boxes[i].intersects(boxes[j]))return true;
    }
    return false;
}
// The one-blur-of-the-union rule for the halo. Deliberately not taken: the halo channel is a linear
// Gaussian, so max-unioning per-glyph halos is not the same image as blurring the unioned coverage,
// and the firmware gives no evidence for either (audit/TEXT-RASTER.md 1.5, 3.2). The halo therefore
// stays per glyph (the existing atlas green channel); only ink and shadow read the union mask.
}

// --- The firmware text material (P3) --------------------------------------------------------------
const Materials::TextMaterial& TextRenderer::defaultMaterial() {
    static const Materials::TextMaterial material=[] {
        Materials::TextMaterial value;
        // 4.93's Text objects carry one shadow block almost everywhere: 1304 of 1305 templates are
        // shadowX=4/shadowY=-5, 1197 are RGB .25 (107 are .9, one disables the shadow), and all are
        // alpha 1 with perspective 0 (audit/TEXT-RENDERER.md "RCO Text attributes for Phase 3",
        // audit/RCO-OBJECT-MODEL.md 9.4). Where a call site has no attributable object - the item
        // labels are paf text built from XMBML at run time, and no RCO in the local 4.93 tree holds a
        // label Text object - this block is the only firmware authority, so it is the default rather
        // than a constant of this renderer's own. `firmware/text-template` in the XMB_TEXT_MATERIAL
        // trace names that provenance, not a container read.
        value.present=true;
        value.container=QStringLiteral("firmware");
        value.object=QStringLiteral("text-template");
        value.size=0;                    // 0: the plugin supplies the size (paf), as the objects store it
        value.top=QColor::fromRgbF(1,1,1,1);
        value.bottom=value.top;
        value.spacingHorizontal=0;
        value.spacingVertical=0;
        value.shadow=QColor::fromRgbF(.25,.25,.25,1.);
        value.shadowX=4;
        value.shadowY=-5;
        value.shadowPerspective=0;
        return value;
    }();
    return material;
}

TextRenderer::ShadowStyle TextRenderer::shadowStyle(const Materials::TextMaterial& material) {
    ShadowStyle style;
    if(!material.present) return style;
    const QColor colour=material.shadow.isValid()?material.shadow:QColor::fromRgbF(0,0,0,0);
    if(colour.alphaF()<=0) return style;             // the one Text object that disables the shadow
    style.on=true;
    style.colour=colour;
    // Frame sign: PAF object space is y-up (the notification's three-label set steps +0.5, -0.375,
    // -0.5 from the top line down), so shadowY -5 is 5 px DOWN on screen. Neither field is scaled by
    // the object's size: the reference clock, drawn at the labels' 27 px, measures its shadow 5 px
    // right and 4-5 px down (audit/TEXT-RENDERER.md), which is the firmware 4/-5 at 1 scene px per
    // unit. Scaling by size/10 would put it at 10.8/13.5 px and does not match the frame.
    style.dx=material.shadowX;
    style.dy=-material.shadowY;
    return style;
}

void TextRenderer::noteMaterial(const Materials::TextMaterial& material,const char* labelClass,int drawnSize,
                                bool shadow,bool ink) {
    // One line per surface, the first time it draws: the material's provenance plus every field, so a
    // later pass can diff this against Materials::textMaterial for the same object and prove the draw
    // used the firmware values rather than agreeing with them by coincidence.
    if(!materialTrace_) return;
    const QString name=labelClass && *labelClass?QString::fromLatin1(labelClass):QStringLiteral("unattributed");
    const auto style=shadowStyle(material);
    // One line per (surface, drawn size, shadow applied or not): the focused label's glow pass draws
    // the same 27 px class with shadow=false, so the key must keep the two draws apart or the trace
    // would describe the glow pass and hide the shadow the ink pass actually applies.
    const bool withShadow=shadow && style.on;
    const QString key=name+QChar(0x1f)+QString::number(drawnSize)+QChar(0x1f)+(withShadow?QChar('s'):QChar('n'));
    const auto found=materialUses_.constFind(key);
    if(found!=materialUses_.cend()) { ++materialUses_[key]; return; }
    materialUses_.insert(key,1);
    const QString source=material.present && !material.container.isEmpty()
        ?material.container+'/'+material.object:QStringLiteral("<absent>");
    const QString shadowColour=material.shadow.isValid()
        ?QStringLiteral("%1,%2,%3").arg(material.shadow.redF(),0,'f',3).arg(material.shadow.greenF(),0,'f',3)
            .arg(material.shadow.blueF(),0,'f',3):QStringLiteral("<invalid>");
    const QString applied=!style.on?QStringLiteral("off")
        :(!shadow?QStringLiteral("off (shadow=false)"):ink?QStringLiteral("shadow"):QStringLiteral("shadow (ink=false)"));
    qInfo().noquote()<<QString("text material: class=%1 drawn=%2px source=%3 size=%4 spacing=(%5,%6) "
        "font=%7 shadow=(%8,%9) screen=(%10,%11) scene px colour=(%12) alpha=%13 applied=%14")
        .arg(name).arg(drawnSize).arg(source).arg(material.size,0,'g',6)
        .arg(material.spacingHorizontal,0,'g',6).arg(material.spacingVertical,0,'g',6)
        .arg(material.fontReference.isEmpty()?QStringLiteral("<null>"):material.fontReference)
        .arg(material.shadowX,0,'g',6).arg(material.shadowY,0,'g',6)
        .arg(style.dx,0,'f',2).arg(style.dy,0,'f',2).arg(shadowColour)
        .arg(style.colour.alphaF(),0,'f',2).arg(applied);
    // Assertion, per surface: the material exists and lies in the domain the two draw paths can
    // express. A material with no shadow block is not an error (one object disables it), so the
    // assertion is about consistency, not about the shadow being on.
    const bool ok=material.present && std::isfinite(material.shadowX) && std::isfinite(material.shadowY)
        && (!material.shadow.isValid() || (material.shadow.alphaF()>=0 && material.shadow.alphaF()<=1));
    const qreal pad=std::ceil(haloRadius*std::max(1.,rasterScale_))+2.;
    const bool fits=!style.on || (std::abs(style.dx)*rasterScale_<=pad && std::abs(style.dy)*rasterScale_<=pad);
    qInfo().noquote()<<QString("text material: %1 class=%2 shadowX=%3 shadowY=%4 shadowAlpha=%5 "
        "offset-within-tile-pad=%6")
        .arg(ok?QStringLiteral("PASS"):QStringLiteral("FAIL"),name)
        .arg(material.shadowX,0,'g',6).arg(material.shadowY,0,'g',6)
        .arg(material.shadow.isValid()?material.shadow.alphaF():-1.,0,'f',2)
        .arg(!style.on?QStringLiteral("n/a"):fits?QStringLiteral("yes"):QStringLiteral("no (mask path or clipped)"));
}

void TextRenderer::draw(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
                        const QString& text,const QFont& requested,const QColor& colour,int flags,
                        qreal glow,bool shadow,bool ink,const char* labelClass) {
    drawImpl(gl,painter,rect,text,requested,colour,flags,defaultMaterial(),labelClass,glow,shadow,ink);
}

void TextRenderer::draw(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
                        const QString& text,const QFont& requested,const QColor& colour,int flags,
                        const Materials::TextMaterial& material,const char* labelClass,
                        qreal glow,bool shadow,bool ink) {
    drawImpl(gl,painter,rect,text,requested,colour,flags,material,labelClass,glow,shadow,ink);
}

void TextRenderer::draw(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
                        const QString& text,const QFont& requested,const QColor& colour,int flags,
                        Materials& materials,const QString& container,const QString& object,const char* labelClass,
                        qreal glow,bool shadow,bool ink) {
    // The label path's entry point: the object is known here, so the material is read from the RCO
    // tree (Materials::textMaterial) and cached. An object that does not resolve leaves present false
    // and the draw falls back to defaultMaterial(), which the trace reports as absent.
    const QString key=container+QChar(0x1f)+object;
    auto found=materialCache_.constFind(key);
    if(found==materialCache_.cend()) found=materialCache_.insert(key,materials.textMaterial(container,object));
    // An object that does not resolve must not pass silently: the draw still happens with the firmware
    // default, so the trace says which object was asked for and that it was absent, once per object.
    if(!found->present && materialTrace_ && !materialUses_.contains(QStringLiteral("absent:")+key)) {
        materialUses_.insert(QStringLiteral("absent:")+key,1);
        qWarning().noquote()<<QString("text material: %1/%2 did not resolve (container or object absent); "
            "the draw uses the firmware Text template default").arg(container,object);
    }
    drawImpl(gl,painter,rect,text,requested,colour,flags,*found,labelClass,glow,shadow,ink);
}

TextRenderer::~TextRenderer() { joinWarm(); }

void TextRenderer::joinWarm() {
    if(warmThread_.joinable())warmThread_.join();
}

// Shapes one short string per firmware family with the same QFont path the draws use. The result is
// discarded: this only resolves Qt's application-font engines and shaper startup, a fixed one-time cost
// of the first real layout (measured ~150 ms with any firmware face; a system face costs under 1 ms and
// a second firmware family is under 1 ms, so the cost is process-global). No renderer state is touched,
// so the draws that follow are unchanged; only when the cost is paid moves, off the first paint.
static void warmFontStackStatic(const QStringList& families) {
    const auto shape=[&](const QString& family) {
        QFont font(family);font.setStyleName(QStringLiteral("Regular"));font.setPixelSize(42);
        font.setStyleStrategy(QFont::PreferAntialias);font.setHintingPreference(QFont::PreferNoHinting);
        QTextLayout lay(QStringLiteral("Game"),font);
        QTextOption option;option.setWrapMode(QTextOption::NoWrap);lay.setTextOption(option);
        lay.beginLayout();
        auto line=lay.createLine();
        if(line.isValid()){line.setLineWidth(1e9);line.setPosition({0,0});}
        lay.endLayout();
    };
    for(const auto& family:families)if(!family.isEmpty())shape(family);
}


bool TextRenderer::init(QOpenGLFunctions_3_3_Core& gl,const QString& fontDirectory) {
    joinWarm(); // a re-created context must not reuse a joinable std::thread
    profile_=qEnvironmentVariableIntValue("XMB_TEXT_PROFILE")!=0;
    // The startup budget's own split (XMB_TEXT_PROFILE=1): when init starts, how much of it is the font
    // configure, and how much the three shader programs. Diagnostic only; no timer runs on the shipped path.
    QElapsedTimer profileClock,profileTotal;
    if(profile_){profileClock.start();profileTotal.start();qInfo().noquote()<<"text profile: init start";}
    useFreeType_=raster_.configure(fontDirectory) && !qEnvironmentVariableIntValue("XMB_TEXT_QT");
    const qint64 profileFontNs=profile_?profileClock.nsecsElapsed():0;
    // Start the font-stack warm-up before the shader programs, so its one-time cost overlaps the rest of
    // GL init and the scene work before the first text draw. XMB_TEXT_WARM=0 restores the synchronous
    // first-draw cost for the A/B.
    if(useFreeType_ && qEnvironmentVariable("XMB_TEXT_WARM")!=QStringLiteral("0"))
        warmThread_=std::thread([families=raster_.families()]{ warmFontStackStatic(families); });
    // Per-phase coverage tiles are on by default: two phases per layer pixel on each axis, i.e. four
    // positions per output pixel. XMB_TEXT_PHASE=0 restores whole-output-pixel snapping.
    const bool phasesEnabled=qEnvironmentVariableIsSet("XMB_TEXT_PHASE")
        ?qEnvironmentVariableIntValue("XMB_TEXT_PHASE")!=0:true;
    phases_=useFreeType_ && phasesEnabled;
    // Coverage union: auto by default (only strings whose glyph inks overlap), XMB_TEXT_MASK=0 keeps
    // the per-glyph alpha-over fast path everywhere, XMB_TEXT_MASK=1 forces the mask for every
    // ink/shadow string so the path can be A/B-ed and timed. The firmware rule itself is not optional
    // evidence: it is the compare-and-keep-larger copy at FUN_004057a0 (audit/TEXT-RASTER.md 1.4-1.5).
    maskMode_=qEnvironmentVariableIsSet("XMB_TEXT_MASK")?qEnvironmentVariableIntValue("XMB_TEXT_MASK"):-1;
    materialTrace_=qEnvironmentVariableIntValue("XMB_TEXT_MATERIAL")!=0;
    // Shadow composite. The shipped mapping is the multiplicative one: the material's shadow colour
    // darkens the layer (in linear light shadowRGB is the factor; the layer is display-referred, so
    // pow(rgb, 1/2.2) is what gets multiplied). That is the composite the reference frame measures -
    // see "Text shadow from the firmware material" in audit/TEXT-RENDERER.md. XMB_TEXT_SHADOW=premultiplied
    // restores the literal premultiplied colour/alpha reading for the A/B; diagnostic only, the capture
    // harness leaves it unset.
    shadowAttenuate_=qEnvironmentVariable("XMB_TEXT_SHADOW").trimmed()
        .compare(QStringLiteral("premultiplied"),Qt::CaseInsensitive)!=0;
    if(materialTrace_) {
        const auto& material=defaultMaterial();
        const auto style=shadowStyle(material);
        qInfo().noquote()<<QString("text material: trace on; shadow from the firmware material, "
            "mapping offset=(shadowX,-shadowY) scene px, colour=shadowRGB multiplies the layer as "
            "pow(rgb,1/2.2), coverage scaled by shadowAlpha*textAlpha (the composite is a fit; the firmware "
            "shadow blend state is unrecovered)");
        qInfo().noquote()<<QString("text material: default = firmware Text template (4.93: 1304/1305 objects "
            "shadowX=%1 shadowY=%2 RGB=%3 alpha=%4) -> screen (+%5,+%6) scene px; no RCO Text object holds an "
            "item label (paf draws XMBML text at run time)")
            .arg(material.shadowX).arg(material.shadowY)
            .arg(QStringLiteral("%1,%2,%3").arg(material.shadow.redF(),0,'f',2)
                     .arg(material.shadow.greenF(),0,'f',2).arg(material.shadow.blueF(),0,'f',2))
            .arg(style.colour.alphaF(),0,'f',2).arg(style.dx,0,'f',2).arg(style.dy,0,'f',2);
        // Prove the read path the call sites use, from inside the renderer: the font directory is
        // <assets root>/data/font, so the firmware root is two levels up. xmb_plugin_normal is the
        // XMB's own container and its `clock` Text object is the one text surface in the frame that
        // has both an object and a name the renderer's callers already use. The read goes through the
        // same Materials::textMaterial the label hunks call, so a match here is authority for the
        // default, not agreement. Diagnostic only: this reader instance exists under
        // XMB_TEXT_MATERIAL=1, never on the shipped path.
        const QString flashRoot=QDir::cleanPath(QDir(fontDirectory).absoluteFilePath(QStringLiteral("../..")));
        Assets firmware(flashRoot);
        RcoObjects objects(firmware);
        Materials materials(firmware,objects);
        const auto clock=materials.textMaterial(QStringLiteral("xmb_plugin_normal"),QStringLiteral("clock"));
        const auto& fallback=defaultMaterial();
        qInfo().noquote()<<QString("text material: RCO read %1/%2 present=%3 size=%4 shadow=(%5,%6) "
            "RGB=(%7,%8,%9) alpha=%10 spacing=(%11,%12) font=%13 container=%14")
            .arg(clock.container,clock.object).arg(clock.present).arg(clock.size,0,'g',6)
            .arg(clock.shadowX,0,'g',6).arg(clock.shadowY,0,'g',6)
            .arg(clock.shadow.redF(),0,'f',3).arg(clock.shadow.greenF(),0,'f',3).arg(clock.shadow.blueF(),0,'f',3)
            .arg(clock.shadow.alphaF(),0,'f',2)
            .arg(clock.spacingHorizontal,0,'g',6).arg(clock.spacingVertical,0,'g',6)
            .arg(clock.fontReference.isEmpty()?QStringLiteral("<null>"):clock.fontReference)
            .arg(objects.container(clock.container).ok?QStringLiteral("read"):QStringLiteral("absent"));
        const bool same=clock.present && clock.shadowX==fallback.shadowX && clock.shadowY==fallback.shadowY
            && std::abs(clock.shadow.redF()-fallback.shadow.redF())<1e-3
            && std::abs(clock.shadow.alphaF()-fallback.shadow.alphaF())<1e-3;
        qInfo().noquote()<<QString("text material: %1 default == xmb_plugin_normal/clock shadow block "
            "(shadow %2/%3 vs %4/%5; colour %6 vs %7; alpha %8 vs %9)")
            .arg(same?QStringLiteral("PASS"):QStringLiteral("FAIL"))
            .arg(clock.shadowX,0,'g',3).arg(clock.shadowY,0,'g',3)
            .arg(fallback.shadowX,0,'g',3).arg(fallback.shadowY,0,'g',3)
            .arg(clock.shadow.isValid()?QStringLiteral("%1,%2,%3").arg(clock.shadow.redF(),0,'f',3)
                     .arg(clock.shadow.greenF(),0,'f',3).arg(clock.shadow.blueF(),0,'f',3):QStringLiteral("<absent>"))
            .arg(QStringLiteral("%1,%2,%3").arg(fallback.shadow.redF(),0,'f',3)
                     .arg(fallback.shadow.greenF(),0,'f',3).arg(fallback.shadow.blueF(),0,'f',3))
            .arg(clock.shadow.isValid()?clock.shadow.alphaF():-1.,0,'f',2)
            .arg(fallback.shadow.alphaF(),0,'f',2);
    }
    const char* vertex=R"(#version 330 core
layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;layout(location=2) in vec4 bounds;
uniform vec2 viewport;out vec2 uv;flat out vec4 tileBounds;
void main(){uv=texcoord;tileBounds=bounds;gl_Position=vec4(position.x*2./viewport.x-1.,1.-position.y*2./viewport.y,0.,1.);}
)";
    const char* fragment=R"(#version 330 core
in vec2 uv;flat in vec4 tileBounds;out vec4 result;uniform sampler2D atlas;uniform vec4 tint;
uniform vec2 shadowOffset;uniform float glowStrength,shadowAlpha;uniform bool ink,shadowPass;uniform vec3 shadowColor;
uniform bool shadowAttenuate;
void main(){
    vec2 coverage=texture(atlas,uv).rg;
    vec2 shadowUv=uv-shadowOffset;
    float inside=step(tileBounds.x,shadowUv.x)*step(tileBounds.y,shadowUv.y)*step(shadowUv.x,tileBounds.z)*step(shadowUv.y,tileBounds.w);
    float shadow=texture(atlas,shadowUv).r*inside*shadowAlpha*tint.a;
    // Two shadow composites, one switch: premultiplied (the material's colour at the material's alpha,
    // normal alpha-over) or attenuate (multiply the destination by shadowRGB's linear-light value; the
    // layer is display-space, so pow(rgb,1/2.2) is the factor). See audit/TEXT-RENDERER.md.
    if(shadowPass){
        if(shadowAttenuate){result=vec4(mix(vec3(1.),pow(shadowColor,vec3(1./2.2)),clamp(shadow,0.,1.)),1.);return;}
        result=vec4(shadowColor*shadow,shadow);return;
    }
    float halo=clamp(coverage.g*glowStrength,0.,1.)*tint.a;
    vec3 rgb=vec3(.922,.961,1.)*halo;
    float fill=ink?coverage.r*tint.a:0.;
    result=vec4(tint.rgb*fill+rgb*(1.-fill),fill+halo*(1.-fill));
}
)";
    if(profile_)profileClock.restart();
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) ||
       !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !program_.link()) {
        qWarning().noquote()<<"text atlas:"<<program_.log();return false;
    }
    // The mask pass reads the same atlas tile but writes coverage only (the red channel) into an R8
    // target. GL_MAX then implements FUN_004057a0's compare-and-keep-larger across glyphs.
    const char* maskFragment=R"(#version 330 core
in vec2 uv;out vec4 result;uniform sampler2D atlas;
void main(){result=vec4(texture(atlas,uv).r,0.,0.,1.);}
)";
    // The composite pass reads the union mask once per string. The shadow is the mask at the offset,
    // not a per-tile copy, and ink is tint * coverage, both premultiplied like the fast path.
    const char* compositeFragment=R"(#version 330 core
in vec2 uv;out vec4 result;uniform sampler2D mask;uniform vec4 tint;uniform vec3 shadowColor;
uniform float shadowAlpha;uniform bool ink,shadowPass;uniform vec2 shadowOffset;uniform bool shadowAttenuate;
void main(){
    float coverage=texture(mask,uv-(shadowPass?shadowOffset:vec2(0.))).r;
    if(shadowPass){
        float shadow=coverage*shadowAlpha*tint.a;
        if(shadowAttenuate){result=vec4(mix(vec3(1.),pow(shadowColor,vec3(1./2.2)),clamp(shadow,0.,1.)),1.);return;}
        result=vec4(shadowColor*shadow,shadow);return;
    }
    float fill=ink?coverage*tint.a:0.;
    result=vec4(tint.rgb*fill,fill);
}
)";
    const qint64 profileShaderNs=profile_?profileClock.nsecsElapsed():0;
    if(profile_)profileClock.restart();
    maskAvailable_=maskProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) &&
        maskProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment,maskFragment) && maskProgram_.link() &&
        compositeProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) &&
        compositeProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment,compositeFragment) &&
        compositeProgram_.link();
    if(!maskAvailable_)
        qWarning().noquote()<<"text mask:"<<maskProgram_.log()<<compositeProgram_.log()
            <<"- coverage union disabled, per-glyph alpha-over stays";
    const qint64 profileMaskShaderNs=profile_?profileClock.nsecsElapsed():0;
    if(profile_)profileClock.restart();
    gl.glGenVertexArrays(1,&vao_);gl.glGenBuffers(1,&vbo_);
    gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    gl.glEnableVertexAttribArray(1);gl.glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(2*sizeof(float)));
    gl.glEnableVertexAttribArray(2);gl.glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(4*sizeof(float)));
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);ready_=true;
    qInfo()<<"text atlas:"<<(useFreeType_?raster_.version()+"; firmware flags 10 (no hinting/no bitmap)":QString("Qt coverage fallback; vertical hinting"))
           <<"grayscale coverage; measured Gaussian halo;"
           <<(phases_?"per-phase tiles (XMB_TEXT_PHASE)":"whole-output-pixel glyph origins")
           <<"coverage union:"<<(maskAvailable_
               ?(maskMode_>0?QString("forced (XMB_TEXT_MASK=1)"):maskMode_<0?QString("auto on overlapping ink"):QString("off (XMB_TEXT_MASK=0)"))
               :QString("unavailable"))
           <<"shadow:"<<(shadowAttenuate_?QString("attenuate, fitted (default)")
                                          :QString("premultiplied colour/alpha (XMB_TEXT_SHADOW=premultiplied)"));
    if(profile_) {
        const auto ms=[](qint64 ns){return QString::number(double(ns)/1e6,'f',3);};
        const qint64 totalNs=profileTotal.nsecsElapsed();
        qInfo().noquote()<<QString("text profile: init total %1 ms (font configure %2 ms, atlas shader %3 ms, "
            "mask shaders %4 ms, vao %5 ms)")
            .arg(ms(totalNs)).arg(ms(profileFontNs)).arg(ms(profileShaderNs)).arg(ms(profileMaskShaderNs))
            .arg(ms(totalNs-profileFontNs-profileShaderNs-profileMaskShaderNs));
    }
    return true;
}
void TextRenderer::release(QOpenGLFunctions_3_3_Core& gl) {
    joinWarm();
    if(profile_)profileFlush("release");
    for(const auto& page:pages_)gl.glDeleteTextures(1,&page.texture);
    pages_.clear();faces_.clear();layouts_.clear();
    // A new context re-reads the material objects through the caller's Materials, so the cached copies
    // and the "already traced" set do not outlive this renderer.
    materialCache_.clear();materialUses_.clear();
    if(maskFbo_)gl.glDeleteFramebuffers(1,&maskFbo_);
    if(maskTexture_)gl.glDeleteTextures(1,&maskTexture_);
    maskFbo_=maskTexture_=0;maskSize_=QSize();maskAvailable_=false;
    if(vbo_)gl.glDeleteBuffers(1,&vbo_);if(vao_)gl.glDeleteVertexArrays(1,&vao_);
    vbo_=vao_=0;program_.removeAllShaders();maskProgram_.removeAllShaders();
    compositeProgram_.removeAllShaders();ready_=false;maskLogged_=false;
}
void TextRenderer::beginFrame(QOpenGLFunctions_3_3_Core& gl,const QSize& target,qreal rasterScale,int pixelGrid) {
    if(profile_ && profileGlyphs_)profileFlush("frame");
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
    QElapsedTimer profileClock;
    if(profile_)profileClock.start();
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
    if(profile_){profileRasterNs_+=profileClock.nsecsElapsed();profileClock.restart();}
    const auto tile=tileFor(coverage,pad,std::max(.5,haloSigma*rasterScale_));
    // Tight ink bounds (nonzero red coverage only) in tile pixels. The halo padding is excluded so the
    // overlap test asks whether two glyphs can actually share a covered layer pixel, not whether their
    // blur kernels touch. This is what decides the mask path for a string.
    QRect inkBounds;
    for(int y=0;y<coverage.height();++y) {
        const uchar* row=coverage.constScanLine(y);
        for(int x=0;x<coverage.width();++x)
            if(row[x]){inkBounds=inkBounds.united(QRect(x,y,1,1));}
    }
    if(!inkBounds.isEmpty())value.ink=inkBounds.adjusted(pad,pad,pad,pad);
    if(profile_){profileHaloNs_+=profileClock.nsecsElapsed();profileClock.restart();}
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
        if(profile_)profileClock.restart();
        Page p;gl.glGenTextures(1,&p.texture);gl.glBindTexture(GL_TEXTURE_2D,p.texture);
        // Empty areas and gutters must be zero for bilinear sampling at tile edges.
        QImage empty(atlasSize,atlasSize,QImage::Format_RGBA8888);empty.fill(Qt::transparent);
        gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,atlasSize,atlasSize,0,GL_RGBA,GL_UNSIGNED_BYTE,empty.constBits());
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        pages_.push_back(p);
        if(profile_){profilePageNs_+=profileClock.nsecsElapsed();++profilePages_;profileClock.restart();}
    }
    auto& p=pages_.back();value.page=int(pages_.size())-1;value.atlas={p.x,p.y,tile.width(),tile.height()};
    const auto bounds=f.font.boundingRect(index);
    value.bearing=raster.valid?raster.bearing-QPoint(pad,pad):
        QPoint(int(std::floor(bounds.left()))-pad,int(std::floor(bounds.top()))-pad);
    gl.glBindTexture(GL_TEXTURE_2D,p.texture);gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    gl.glTexSubImage2D(GL_TEXTURE_2D,0,p.x,p.y,tile.width(),tile.height(),GL_RGBA,GL_UNSIGNED_BYTE,tile.constBits());
    if(profile_){profileUploadNs_+=profileClock.nsecsElapsed();++profileGlyphs_;}
    p.x+=tile.width()+1;p.rowHeight=std::max(p.rowHeight,tile.height());
    return f.glyphs.insert(key,value).value();
}
TextRenderer::Layout& TextRenderer::layout(const QString& text,const QFont& font,qreal width,int flags) {
    const QString display=FontRaster::mapSymbols(text);
    const QString key=font.key()+QChar(0x1f)+QString::number(width,'f',3)+':'+QString::number(flags)+QChar(0x1f)+display;
    joinWarm(); // the one-time Qt font-stack cost, if the worker has not finished it yet
    auto found=layouts_.find(key);if(found!=layouts_.end()){found->used=++serial_;return *found;}
    QElapsedTimer shapeClock;if(profile_)shapeClock.start();
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
        if(profile_)profileShapeLineNs_+=shapeClock.nsecsElapsed();
        if(!any)value.height+=QFontMetricsF(font).height();
        for(const auto& run:shaped.glyphRuns()) {
            const int f=face(run.rawFont());const auto indices=run.glyphIndexes();const auto positions=run.positions();
            for(int i=0;i<indices.size();++i)value.glyphs.push_back({f,indices[i],positions[i]});
        }
    }
    if(profile_) {
        const qint64 shapeNs=shapeClock.nsecsElapsed();
        if(shapeNs>=2000000)qInfo().noquote()<<QString("text profile: layout %1 ms for %2 px \"%3\" "
            "(lines+layout %4 ms, glyph runs %5 ms)")
            .arg(double(shapeNs)/1e6,0,'f',2).arg(font.pixelSize()).arg(display.left(28))
            .arg(double(profileShapeLineNs_)/1e6,0,'f',2)
            .arg(double(shapeNs-profileShapeLineNs_)/1e6,0,'f',2);
        profileShapeLineNs_=0;
    }
    return layouts_.insert(key,std::move(value)).value();
}
void TextRenderer::drawImpl(QOpenGLFunctions_3_3_Core& gl,QPainter& painter,const QRectF& rect,
                            const QString& text,const QFont& requested,const QColor& colour,int flags,
                            const Materials::TextMaterial& material,const char* labelClass,
                            qreal glow,bool shadow,bool ink) {
    if(!ready_ || text.isEmpty() || colour.alpha()==0 || painter.opacity()<=0)return;
    QElapsedTimer profileDrawClock;
    if(profile_){profileDrawClock.start();++profileDraws_;}
    // The material this string actually draws with, and the shadow it reduces to. The material is
    // traced once per surface before any GL state changes, so the log's first line is a complete
    // description of the shadow the following draw applies.
    const Materials::TextMaterial& applied=material.present?material:defaultMaterial();
    const ShadowStyle style=shadowStyle(applied);
    noteMaterial(applied,labelClass,requested.pixelSize(),shadow,ink);
    // A material whose shadow is disabled skips the pass even when the call site asked for one.
    const bool withShadow=shadow && style.on;
    QFont font=requested;
    if(font.styleName().isEmpty() && font.weight()==QFont::Normal && !font.italic())font.setStyleName("Regular");
    font.setPixelSize(std::max(1,int(std::lround(requested.pixelSize()*rasterScale_))));
    font.setStyleStrategy(QFont::PreferAntialias);
    // The firmware never hints: libfontFT loads glyphs with FT_LOAD_NO_HINTING (audit/TEXT-RASTER.md), so both
    // rasteriser paths must stay unhinted or stems snap differently from the reference.
    font.setHintingPreference(QFont::PreferNoHinting);
    auto& shaped=layout(text,font,rect.width()*rasterScale_,flags);
    if(profile_)profileShapeNs_+=profileDrawClock.nsecsElapsed();
    qreal offset=0;if(flags&Qt::AlignVCenter)offset=(rect.height()*rasterScale_-shaped.height)/2;
    else if(flags&Qt::AlignBottom)offset=rect.height()*rasterScale_-shaped.height;
    const auto transform=painter.combinedTransform();
    const qreal opacity=painter.opacity()*colour.alphaF();
    const bool clipped=painter.hasClipping();const QRect clip=transform.mapRect(painter.clipBoundingRect()).toAlignedRect().intersected(QRect(QPoint(),target_));
    painter.beginNativePainting();
    gl.glViewport(0,0,target_.width(),target_.height());
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_STENCIL_TEST);gl.glDisable(GL_CULL_FACE);
    // The painter's own clip stays in force for the layer composite; the mask pass replaces it with the
    // string box and restores this one before it writes to the layer.
    const auto applyClip=[&]{
        if(clipped){gl.glEnable(GL_SCISSOR_TEST);gl.glScissor(clip.x(),target_.height()-clip.bottom()-1,clip.width(),clip.height());}
        else gl.glDisable(GL_SCISSOR_TEST);
    };
    applyClip();
    gl.glEnable(GL_BLEND);gl.glBlendEquation(GL_FUNC_ADD);gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindSampler(0,0);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    vertexBuffer_.clear();batchBuffer_.clear();boxBuffer_.clear();
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
        if(profile_)++profileQuads_;
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
        if(batchBuffer_.isEmpty() || batchBuffer_.back().page!=g.page)batchBuffer_.push_back({g.page,int(vertexBuffer_.size()),0});
        const auto vertex=[&](const QPointF& p,float s,float t){return Vertex{float(p.x()),float(p.y()),s,t,u,v,r,b};};
        vertexBuffer_<<vertex(tl,u,v)<<vertex(bl,u,b)<<vertex(tr,r,v)<<vertex(tr,r,v)<<vertex(bl,u,b)<<vertex(br,r,b);
        batchBuffer_.back().count+=6;
        if(!g.ink.isEmpty()) {
            const qreal unitX=width/g.atlas.width(),unitY=height/g.atlas.height();
            boxBuffer_.push_back(QRectF(originX+g.ink.x()*unitX,originY+g.ink.y()*unitY,
                                        g.ink.width()*unitX,g.ink.height()*unitY));
        }
    }
    const int glyphVertexCount=int(vertexBuffer_.size());
    // The shadow's displacement in layer pixels: the material stores it in 1920x1080 scene pixels,
    // while the atlas tiles, the union mask and the layer are in layer pixels (rasterScale_ of them
    // per scene pixel).
    const qreal shadowDx=style.dx*rasterScale_,shadowDy=style.dy*rasterScale_;
    // Mask mode only when the string can actually need the union: two glyph inks must intersect.
    // XMB_TEXT_MASK=0 forces the fast path, XMB_TEXT_MASK=1 forces the mask for the A/B and timing run.
    const bool unionNeeded=(withShadow||ink) && maskAvailable_ && maskMode_!=0
        && (maskMode_>0 || anyOverlap(boxBuffer_));
    QRect clearBox,quadBox;
    bool masked=false;
    if(unionNeeded) {
        QRect content;
        for(const auto& box:boxBuffer_)content=content.united(box.toAlignedRect());
        QElapsedTimer profileMaskClock;bool haveMask=false;
        if(!content.isEmpty()){if(profile_)profileMaskClock.start();haveMask=ensureMask(gl);if(profile_)profileMaskNs_+=profileMaskClock.nsecsElapsed();}
        if(haveMask) {
            // The shadow samples the same mask displaced by (shadowDx, shadowDy) layer px, so the
            // cleared region is the ink box grown by that displacement on every side (a cleared edge
            // texel is still zero) and the composite quad is the ink box plus the displaced copy.
            // Both are clamped to the layer.
            const int left=std::max(0,-int(std::floor(shadowDx))),top=std::max(0,-int(std::floor(shadowDy)));
            const int right=std::max(0,int(std::ceil(shadowDx))),bottom=std::max(0,int(std::ceil(shadowDy)));
            const QRect targetRect(QPoint(0,0),target_);
            clearBox=content.adjusted(-left-1,-top-1,right+1,bottom+1).intersected(targetRect);
            quadBox=content.adjusted(-left,-top,right,bottom).intersected(targetRect);
            masked=!clearBox.isEmpty() && !quadBox.isEmpty();
            if(masked) {
                // One quad over the composite box, appended after the glyph range. Its uv is a direct
                // 1:1 map of the layer (v is flipped because the mask was rendered with the same
                // top-down vertex transform: layer row y lands in texture row height-1-y).
                const qreal w=target_.width(),h=target_.height();
                const QRectF quad(quadBox);
                const auto cvertex=[&](const QPointF& p){
                    return Vertex{float(p.x()),float(p.y()),float(p.x()/w),float(1.-p.y()/h),0,0,0,0};
                };
                const QPointF ctl(quad.left(),quad.top()),ctr(quad.right(),quad.top());
                const QPointF cbl(quad.left(),quad.bottom()),cbr(quad.right(),quad.bottom());
                vertexBuffer_<<cvertex(ctl)<<cvertex(cbl)<<cvertex(ctr)
                             <<cvertex(ctr)<<cvertex(cbl)<<cvertex(cbr);
            }
        }
    }
    gl.glBufferData(GL_ARRAY_BUFFER,vertexBuffer_.size()*sizeof(Vertex),vertexBuffer_.constData(),GL_STREAM_DRAW);
    const auto drawGlyphPass=[&]{
        for(const auto& batch:batchBuffer_){
            gl.glBindTexture(GL_TEXTURE_2D,pages_[size_t(batch.page)].texture);
            gl.glDrawArrays(GL_TRIANGLES,batch.first,batch.count);
        }
    };
    if(masked) {
        if(!maskLogged_) {
            maskLogged_=true;
            qInfo().noquote()<<QString("text mask: union coverage active (%1); first string box %2x%3 layer px, %4 inked glyphs")
                .arg(maskMode_>0?"forced":"auto").arg(clearBox.width()).arg(clearBox.height()).arg(boxBuffer_.size());
        }
        GLint previousFramebuffer=0;gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previousFramebuffer);
        maskPass(gl,batchBuffer_,clearBox);
        gl.glBindFramebuffer(GL_FRAMEBUFFER,GLuint(previousFramebuffer));
        gl.glViewport(0,0,target_.width(),target_.height());
        applyClip();
        gl.glBlendEquation(GL_FUNC_ADD);gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        // maskPass leaves the vertex array unbound; the halo pass and both composite draws read the
        // vertex buffer that was uploaded before the mask pass.
        gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        compositeProgram_.bind();
        compositeProgram_.setUniformValue("viewport",QVector2D(target_.width(),target_.height()));
        compositeProgram_.setUniformValue("mask",0);
        compositeProgram_.setUniformValue("tint",QVector4D(colour.redF(),colour.greenF(),colour.blueF(),opacity));
        compositeProgram_.setUniformValue("shadowColor",QVector3D(float(style.colour.redF()),float(style.colour.greenF()),
                                                                  float(style.colour.blueF())));
        compositeProgram_.setUniformValue("shadowAlpha",float(style.colour.alphaF()));
        // Screen +y is down and the mask's v is flipped with the layer, so a down-right displacement is
        // (+dx, -dy) in layer-normalised uv: the composite samples the mask at p - offset, which paints
        // the shadow at p + offset.
        compositeProgram_.setUniformValue("shadowOffset",QVector2D(float(shadowDx)/float(target_.width()),
                                                                  -float(shadowDy)/float(target_.height())));
        compositeProgram_.setUniformValue("shadowAttenuate",shadowAttenuate_);
        gl.glActiveTexture(GL_TEXTURE0);gl.glBindSampler(0,0);gl.glBindTexture(GL_TEXTURE_2D,maskTexture_);
        // Shadow first, then the per-tile halo, then the unioned ink: the same layering as the fast path.
        if(withShadow) {
            compositeProgram_.setUniformValue("shadowPass",true);
            compositeProgram_.setUniformValue("ink",false);
            // The attenuate composite multiplies the layer; every other pass is alpha-over.
            if(shadowAttenuate_)gl.glBlendFunc(GL_DST_COLOR,GL_ZERO);
            gl.glDrawArrays(GL_TRIANGLES,glyphVertexCount,6);
            if(shadowAttenuate_)gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        }
        if(glow>0) {
            compositeProgram_.release();
            program_.bind();
            program_.setUniformValue("viewport",QVector2D(target_.width(),target_.height()));
            program_.setUniformValue("atlas",0);
            program_.setUniformValue("tint",QVector4D(colour.redF(),colour.greenF(),colour.blueF(),opacity));
            program_.setUniformValue("glowStrength",float(std::clamp(glow,0.,1.)*1.5));
            program_.setUniformValue("shadowAlpha",float(style.colour.alphaF()));
            program_.setUniformValue("shadowColor",QVector3D(float(style.colour.redF()),float(style.colour.greenF()),
                                                             float(style.colour.blueF())));
            program_.setUniformValue("shadowOffset",QVector2D(0,0));
            program_.setUniformValue("ink",false);
            program_.setUniformValue("shadowPass",false);
            drawGlyphPass();
            program_.release();
            compositeProgram_.bind();
        }
        if(ink) {
            gl.glActiveTexture(GL_TEXTURE0);gl.glBindSampler(0,0);gl.glBindTexture(GL_TEXTURE_2D,maskTexture_);
            compositeProgram_.setUniformValue("shadowPass",false);
            compositeProgram_.setUniformValue("ink",true);
            gl.glDrawArrays(GL_TRIANGLES,glyphVertexCount,6);
        }
        compositeProgram_.release();
        gl.glBindTexture(GL_TEXTURE_2D,0);
    } else {
        program_.bind();
        program_.setUniformValue("viewport",QVector2D(target_.width(),target_.height()));program_.setUniformValue("atlas",0);
        program_.setUniformValue("tint",QVector4D(colour.redF(),colour.greenF(),colour.blueF(),opacity));
        program_.setUniformValue("glowStrength",float(std::clamp(glow,0.,1.)*1.5));
        // The per-tile path paints the shadow from the same atlas tile, displaced by the material in
        // atlas texels (the quad maps one atlas texel to one layer pixel), so the tile's halo padding
        // must be at least the displacement or the shadow is clipped at the glyph's edge. The firmware
        // 4/-5 is 8x10 layer px against the 14 px pad of a 2x layer: it fits, and selfCheck asserts
        // that ('shadow-tile-pad'). A larger material offset takes the mask path instead.
        const qreal shadowPad=std::ceil(haloRadius*std::max(1.,rasterScale_))+2.;
        if(withShadow && (std::abs(shadowDx)>shadowPad || std::abs(shadowDy)>shadowPad) && !shadowPadWarned_) {
            shadowPadWarned_=true;
            qWarning().noquote()<<QString("text shadow: material offset (%1,%2) layer px exceeds the glyph tile "
                "padding (%3 px); the per-tile shadow is clipped (the mask path is not)")
                .arg(shadowDx,0,'f',1).arg(shadowDy,0,'f',1).arg(shadowPad,0,'f',1);
        }
        program_.setUniformValue("shadowAlpha",float(style.colour.alphaF()));
        program_.setUniformValue("shadowColor",QVector3D(float(style.colour.redF()),float(style.colour.greenF()),
                                                        float(style.colour.blueF())));
        program_.setUniformValue("ink",ink);
        program_.setUniformValue("shadowOffset",QVector2D(float(shadowDx/atlasSize),float(shadowDy/atlasSize)));
        program_.setUniformValue("shadowAttenuate",shadowAttenuate_);
        const auto drawPass=[&](bool isShadow){
            program_.setUniformValue("shadowPass",isShadow);
            drawGlyphPass();
        };
        // Draw the whole string's shadow first, including across atlas pages. A later glyph's shadow must never
        // cover an earlier glyph's foreground (especially with kerning, ligatures and combining marks).
        if(withShadow) {
            if(shadowAttenuate_)gl.glBlendFunc(GL_DST_COLOR,GL_ZERO);
            drawPass(true);
            if(shadowAttenuate_)gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        }
        drawPass(false);
        program_.release();
    }
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);gl.glBindTexture(GL_TEXTURE_2D,0);
    if(profile_){const qint64 drawNs=profileDrawClock.nsecsElapsed();profileDrawNs_+=drawNs;
        profileSlowestDrawNs_=std::max(profileSlowestDrawNs_,drawNs);}
    if(!checked_ && qEnvironmentVariableIntValue("XMB_TEXT_CHECK")){checked_=true;selfCheck(gl);}
    painter.endNativePainting();
}

void TextRenderer::profileFlush(const char* where) {
    // One line per frame that rasterised tiles: where the glyph cost actually goes. shape/state are
    // the parts of the same draw calls that are not glyph work (QTextLayout shaping, GL state,
    // painter begin/endNativePainting, glBufferData, the driver's first use of each program).
    if(!profile_ && !profileGlyphs_)return;
    const auto ms=[](qint64 ns){return QString::number(double(ns)/1e6,'f',2);};
    const qint64 glyphNs=profileRasterNs_+profileHaloNs_+profilePageNs_+profileUploadNs_+profileMaskNs_;
    qInfo().noquote()<<QString("text profile: %1 %2 tile(s) in %3 draw(s), %4 glyph quads: raster %5 ms, "
        "halo %6 ms, page init %7 ms (%8 page(s)), upload %9 ms, mask target %10 ms | shaping %11 ms, "
        "draw total %12 ms (slowest draw %13 ms, non-glyph %14 ms)")
        .arg(where).arg(profileGlyphs_).arg(profileDraws_).arg(profileQuads_).arg(ms(profileRasterNs_))
        .arg(ms(profileHaloNs_)).arg(ms(profilePageNs_)).arg(profilePages_).arg(ms(profileUploadNs_))
        .arg(ms(profileMaskNs_)).arg(ms(profileShapeNs_)).arg(ms(profileDrawNs_))
        .arg(ms(profileSlowestDrawNs_)).arg(ms(profileDrawNs_-glyphNs-profileShapeNs_));
    profileRasterNs_=profileHaloNs_=profileUploadNs_=profilePageNs_=profileMaskNs_=0;
    profileGlyphs_=profilePages_=profileDraws_=profileQuads_=0;
    profileShapeNs_=profileDrawNs_=profileSlowestDrawNs_=0;
}

bool TextRenderer::ensureMask(QOpenGLFunctions_3_3_Core& gl) {
    if(!maskAvailable_ || target_.isEmpty())return false;
    if(maskFbo_ && maskSize_==target_)return true;
    GLint previousFramebuffer=0;gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previousFramebuffer);
    if(maskFbo_){gl.glDeleteFramebuffers(1,&maskFbo_);maskFbo_=0;}
    if(maskTexture_){gl.glDeleteTextures(1,&maskTexture_);maskTexture_=0;}
    maskSize_=target_;
    gl.glGenTextures(1,&maskTexture_);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,maskTexture_);
    // R8 is the coverage byte itself: no colour, no alpha, no curve. GL_NEAREST keeps the 1:1 layer map
    // exact (the mask is the same size as the UI layer), so the composite cannot resample the union.
    gl.glTexImage2D(GL_TEXTURE_2D,0,GL_R8,maskSize_.width(),maskSize_.height(),0,GL_RED,GL_UNSIGNED_BYTE,nullptr);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    gl.glGenFramebuffers(1,&maskFbo_);
    gl.glBindFramebuffer(GL_FRAMEBUFFER,maskFbo_);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,maskTexture_,0);
    const GLenum status=gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    gl.glBindFramebuffer(GL_FRAMEBUFFER,GLuint(previousFramebuffer));
    gl.glBindTexture(GL_TEXTURE_2D,0);
    if(status!=GL_FRAMEBUFFER_COMPLETE) {
        qWarning().noquote()<<"text mask: R8 mask target incomplete, status"<<QString::number(status,16);
        if(maskFbo_)gl.glDeleteFramebuffers(1,&maskFbo_);
        if(maskTexture_)gl.glDeleteTextures(1,&maskTexture_);
        maskFbo_=maskTexture_=0;maskSize_=QSize();
        return false;
    }
    qInfo()<<"text mask: R8 coverage target"<<maskSize_;
    return true;
}

void TextRenderer::maskPass(QOpenGLFunctions_3_3_Core& gl,const QVector<Batch>& batches,const QRect& box) {
    if(!maskFbo_ || box.isEmpty())return;
    gl.glBindFramebuffer(GL_FRAMEBUFFER,maskFbo_);
    gl.glViewport(0,0,maskSize_.width(),maskSize_.height());
    gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_STENCIL_TEST);gl.glDisable(GL_CULL_FACE);
    // GL_MAX is the firmware's compare-and-keep-larger, per channel; on R8 only red exists.
    gl.glEnable(GL_BLEND);gl.glBlendEquation(GL_MAX);gl.glBlendFunc(GL_ONE,GL_ONE);
    gl.glEnable(GL_SCISSOR_TEST);
    gl.glScissor(box.x(),maskSize_.height()-box.y()-box.height(),box.width(),box.height());
    // Clear inside the scissor only: one string's box, never the whole layer.
    gl.glClearColor(0,0,0,0);gl.glClear(GL_COLOR_BUFFER_BIT);
    gl.glActiveTexture(GL_TEXTURE0);gl.glBindSampler(0,0);
    maskProgram_.bind();
    maskProgram_.setUniformValue("viewport",QVector2D(maskSize_.width(),maskSize_.height()));
    maskProgram_.setUniformValue("atlas",0);
    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    for(const auto& batch:batches) {
        if(batch.page<0 || batch.page>=int(pages_.size()))continue;
        gl.glBindTexture(GL_TEXTURE_2D,pages_[size_t(batch.page)].texture);
        gl.glDrawArrays(GL_TRIANGLES,batch.first,batch.count);
    }
    maskProgram_.release();
    gl.glBindVertexArray(0);gl.glBindBuffer(GL_ARRAY_BUFFER,0);gl.glBindTexture(GL_TEXTURE_2D,0);
    // The caller restores the framebuffer, viewport, scissor and FUNC_ADD blending.
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

void TextRenderer::selfCheck(QOpenGLFunctions_3_3_Core& gl) {
    // Deterministic assertions for the renderer rules that can be checked without a capture: the
    // elision width rule (4.2), the keep-larger coverage union (1.5) and the union *mask path* itself.
    // Run with XMB_TEXT_CHECK=1; every line is logged, so a scripted run is the evidence.
    if(faces_.empty()){qInfo().noquote()<<"text check: no shaped face yet; skipped";return;}
    // A copy, not a reference: shaping below can register a fallback face and push_back into faces_,
    // which would dangle a reference into the vector.
    const QRawFont raw=faces_.front().font;
    QFont font(raw.familyName());
    if(!raw.styleName().isEmpty())font.setStyleName(raw.styleName());
    font.setPixelSize(27);
    bool pass=true;
    const auto report=[&](const char* name,bool ok,const QString& detail) {
        pass&=ok;qInfo().noquote()<<QString("text check: %1 %2 %3").arg(ok?"PASS":"FAIL",name,detail);
    };
    // The firmware text material's shadow mapping (P3). The default is the block 1304 of the 1305 Text
    // objects in 4.93 carry; the screen offset is (shadowX, -shadowY) scene px and must stay inside the
    // per-tile path's halo padding at this layer scale, or that path would clip the shadow silently.
    {
        const auto& material=defaultMaterial();
        const auto style=shadowStyle(material);
        report("shadow-material",style.on && material.shadowX==4. && material.shadowY==-5.
                   && std::abs(style.dx-4.)<1e-6 && std::abs(style.dy-5.)<1e-6
                   && std::abs(style.colour.redF()-.25)<1e-3 && std::abs(style.colour.alphaF()-1.)<1e-3,
               QString("firmware default shadow (4,-5) .25 grey alpha 1 -> screen (%1,%2) scene px, colour alpha %3")
                   .arg(style.dx,0,'f',2).arg(style.dy,0,'f',2).arg(style.colour.alphaF(),0,'f',2));
        const qreal pad=std::ceil(haloRadius*std::max(1.,rasterScale_))+2.;
        const qreal dx=std::abs(style.dx)*std::max(1.,rasterScale_),dy=std::abs(style.dy)*std::max(1.,rasterScale_);
        report("shadow-tile-pad",dx<=pad && dy<=pad,
               QString("offset %1x%2 layer px <= %3 px glyph-tile padding").arg(dx,0,'f',1).arg(dy,0,'f',1)
                   .arg(pad,0,'f',1));
    }
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
    const qreal pafScene=paf/std::max(1.,rasterScale_);
    const qreal shaped=measure(same,clock);
    report("metric-authority",std::abs(pafScene-shaped)<=1.0,
           QString("PAF advance %1 px vs Qt shaped %2 px at %3 px font").arg(pafScene,0,'f',2).arg(shaped,0,'f',2)
               .arg(raw.pixelSize()/std::max(1.,rasterScale_)));
    // Every size the UI actually draws, not only the clock's: the label loop uses 27, the panels 23/22/21/20,
    // the page heading 19, the subtitle cue 17, the option note 16 and the PS menu badges 14. At each size the
    // PAF 26.6 advance sum and the width the renderer shapes with must agree, because elision, the caret and
    // the glyph positions all come from that one sum: a size-dependent gap would elide at the wrong pen.
    const struct { int size; const char* sample; } sizes[]={
        {27,"Game Data Utility"},{26,"PS3XMBpassword"},{23,"Display All"},{22,"System Information"},
        {21,"Photo Settings"},{20,"Charge Level"},{19,"Sort By"},{17,"A subtitle cue line"},
        {16,"This build cannot play it"},{14,"Triangle"}};
    for(const auto& at:sizes) {
        QRawFont scaled=raw;
        scaled.setPixelSize(std::max(1,int(std::lround(at.size*rasterScale_))));
        const QString sample=QString::fromLatin1(at.sample);
        qreal sum=0;
        for(const quint32 glyph:scaled.glyphIndexesForString(sample))sum+=raster_.advance(scaled,glyph);
        QFont sized=font;
        sized.setPixelSize(at.size);
        const qreal pafWidth=sum/std::max(1.,rasterScale_),shapedWidth=measure(sized,sample);
        report(QString("metric-size-%1").arg(at.size).toLatin1().constData(),
               std::abs(pafWidth-shapedWidth)<=1.0,
               QString("\"%1\" PAF %2 px vs shaped %3 px (delta %4)").arg(sample).arg(pafWidth,0,'f',2)
                   .arg(shapedWidth,0,'f',2).arg(pafWidth-shapedWidth,0,'f',2));
    }
    // The OSK caret asks for one thing: the pen x of the composed prefix. It now comes from measure(), which
    // is this same advance sum, so at the field's 26 px the QPainter fallback must land on the same pen too;
    // otherwise a caret drawn on the fallback would sit elsewhere than the glyphs the atlas places.
    {
        const QString field=QStringLiteral("PS3XMBpassword");
        QFont fieldFont=font;
        fieldFont.setPixelSize(26);
        const qreal byAdvance=measure(fieldFont,field.left(7));
        const qreal byQt=QFontMetricsF(fieldFont).horizontalAdvance(field.left(7));
        report("caret-metric",std::abs(byAdvance-byQt)<=0.5,
               QString("prefix \"%1\" advance %2 px vs Qt metrics %3 px").arg(field.left(7)).arg(byAdvance,0,'f',2).arg(byQt,0,'f',2));
    }
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
    // The auto rule that decides the mask path: intersecting tight ink boxes select it, touching or
    // gapped boxes keep the fast path.
    {
        const QRectF a(10,10,20,20),overlap(29,10,20,20),touching(30,10,20,20),apart(31,10,20,20);
        report("union-detect",anyOverlap({a,overlap}) && !anyOverlap({a,touching}) && !anyOverlap({a,apart}),
               "overlapping ink -> mask; touching or gapped ink -> alpha-over fast path");
    }
    // The union *path*, not just the helper: render two deliberately overlapping copies of 'e' into
    // the real R8 mask with the real GL_MAX pass, read the target back and require it to equal the
    // keep-larger helper pixel for pixel. The same pass also shows the pixels where alpha-over would
    // have differed, so the assertion cannot pass vacuously.
    if(base.valid && base.coverage.width()>1 && useFreeType_ && maskAvailable_ && target_.isValid()) {
        const auto probeGlyphs=raw.glyphIndexesForString(QStringLiteral("e"));
        if(!probeGlyphs.isEmpty() && ensureMask(gl)) {
            const auto& g=glyph(gl,0,probeGlyphs.front(),0,0);
            if(g.page>=0 && !g.ink.isEmpty()) {
                const QPoint pad=base.bearing-g.bearing;
                const int ox=8,oy=8,w=g.atlas.width(),h=g.atlas.height();
                const QRect probeBox(ox+g.ink.x(),oy+g.ink.y(),g.ink.width()+1,g.ink.height());
                const QRect targetRect(QPoint(0,0),target_);
                if(targetRect.contains(probeBox)) {
                    const float u=float(g.atlas.x())/atlasSize,v=float(g.atlas.y())/atlasSize;
                    const float r=float(g.atlas.x()+g.atlas.width())/atlasSize,b=float(g.atlas.y()+g.atlas.height())/atlasSize;
                    const auto probeQuad=[&](int x,int y){
                        const float fx=float(x),fy=float(y),fw=float(w),fh=float(h);
                        return QVector<Vertex>{
                            {fx,fy,u,v,u,v,r,b},{fx,fy+fh,u,b,u,v,r,b},{fx+fw,fy,r,v,u,v,r,b},
                            {fx+fw,fy,r,v,u,v,r,b},{fx,fy+fh,u,b,u,v,r,b},{fx+fw,fy+fh,r,b,u,v,r,b}};
                    };
                    QVector<Vertex> probeVertices=probeQuad(ox,oy);
                    probeVertices+=probeQuad(ox+1,oy);
                    const QVector<Batch> probeBatches{{g.page,0,12}};
                    gl.glBindVertexArray(vao_);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo_);
                    gl.glBufferData(GL_ARRAY_BUFFER,probeVertices.size()*sizeof(Vertex),probeVertices.constData(),GL_STREAM_DRAW);
                    GLint previousFramebuffer=0;gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previousFramebuffer);
                    maskPass(gl,probeBatches,probeBox);
                    gl.glPixelStorei(GL_PACK_ALIGNMENT,1);
                    QVector<uchar> readback(probeBox.width()*probeBox.height(),0);
                    gl.glReadPixels(probeBox.x(),target_.height()-probeBox.y()-probeBox.height(),
                                    probeBox.width(),probeBox.height(),GL_RED,GL_UNSIGNED_BYTE,readback.data());
                    gl.glPixelStorei(GL_PACK_ALIGNMENT,4);
                    gl.glBindFramebuffer(GL_FRAMEBUFFER,GLuint(previousFramebuffer));
                    gl.glViewport(0,0,target_.width(),target_.height());
                    gl.glBlendEquation(GL_FUNC_ADD);gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
                    // The probe scissored to its own box; leave the scissor off so the rest of the
                    // frame's painting is not clipped by it. Every text draw sets its own clip.
                    gl.glDisable(GL_SCISSOR_TEST);
                    // CPU expectations: keep-larger (the helper) and alpha-over (what the old quads did).
                    QImage expected(probeBox.width(),probeBox.height(),QImage::Format_Grayscale8);
                    expected.fill(0);
                    QImage over=expected;
                    const int px=ox+pad.x()-probeBox.x(),py=oy+pad.y()-probeBox.y();
                    unionCoverage(expected,base.coverage,px,py);
                    unionCoverage(expected,base.coverage,px+1,py);
                    for(int y=0;y<base.coverage.height();++y)for(int x=0;x<base.coverage.width();++x) {
                        const int tx=x+px,ty=y+py;
                        if(tx<0 || ty<0 || tx>=over.width() || ty>=over.height())continue;
                        const int value=base.coverage.constScanLine(y)[x];
                        if(!value)continue;
                        uchar* row=over.scanLine(ty);
                        row[tx]=uchar(255-((255-int(row[tx]))*(255-value)+127)/255);
                    }
                    int mismatch=0,differ=0;
                    for(int y=0;y<probeBox.height();++y)for(int x=0;x<probeBox.width();++x) {
                        const int gpu=readback[(probeBox.height()-1-y)*probeBox.width()+x];
                        const int cpu=expected.constScanLine(y)[x];
                        if(gpu!=cpu)++mismatch;
                        if(cpu!=over.constScanLine(y)[x])++differ;
                    }


                    report("union-mask",mismatch==0 && differ>0,
                           QString("GPU GL_MAX mask == keep-larger helper on %1 px; alpha-over differs at %2 px")
                               .arg(probeBox.width()*probeBox.height()).arg(differ));
                } else report("union-mask",false,"probe box outside the layer");
            } else report("union-mask",false,"no atlas tile for e");
        } else report("union-mask",false,"mask target unavailable");
    } else report("union-mask",false,"no coverage or mask target");
    // Report, not gate: how the menu's own strings vote on the auto rule, at the label font.
    if(useFreeType_ && raw.isValid()) {
        const auto votes=[&](const QString& sample,int sceneSize) {
            QFont f(raw.familyName());
            if(!raw.styleName().isEmpty())f.setStyleName(raw.styleName());
            f.setPixelSize(std::max(1,int(std::lround(sceneSize*std::max(1.,rasterScale_)))));
            f.setStyleStrategy(QFont::PreferAntialias);f.setHintingPreference(QFont::PreferNoHinting);
            const auto& lay=layout(sample,f,1e6,Qt::AlignLeft|Qt::AlignVCenter);
            QVector<QRectF> boxes;
            for(const auto& placed:lay.glyphs) {
                const auto& tile=glyph(gl,placed.face,placed.index,
                                       phases_?penPhase(placed.position.x()):0,phases_?penPhase(placed.position.y()):0);
                if(tile.page<0 || tile.ink.isEmpty())continue;
                const qreal x=std::floor(placed.position.x())+tile.bearing.x();
                const qreal y=std::floor(placed.position.y())+tile.bearing.y();
                boxes.push_back(QRectF(x+tile.ink.x(),y+tile.ink.y(),tile.ink.width(),tile.ink.height()));
            }
            return anyOverlap(boxes);
        };
        const QStringList samples{QStringLiteral("Clock"),QStringLiteral("Corrupted Data"),
                                  QStringLiteral("System Update"),QStringLiteral("Game Data Utility"),
                                  QStringLiteral("25/10 12:32")};
        int maskVotes=0;
        for(const auto& sample:samples)if(votes(sample,27))++maskVotes;
        qInfo().noquote()<<QString("text check: union auto rule takes the mask on %1 of %2 menu strings tested")
            .arg(maskVotes).arg(samples.size());
    }
    qInfo().noquote()<<QString("text check: %1").arg(pass?"all assertions passed":"assertion failure");
}
}
