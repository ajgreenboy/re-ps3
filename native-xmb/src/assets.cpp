#include "assets.h"
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QtEndian>
#include <stdexcept>
#include <limits>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <QVector>
#include <QStringDecoder>
#include <QTransform>

namespace xmb {
namespace {
void range(const QByteArray& b, quint64 p, quint64 n) {
    if (p > quint64(b.size()) || n > quint64(b.size())-p) throw std::runtime_error("Asset offset exceeds its container");
}
quint32 u32(const QByteArray& b, quint64 p) { range(b,p,4); return qFromBigEndian<quint32>(b.constData()+p); }
quint16 u16(const QByteArray& b, quint64 p) { range(b,p,2); return qFromBigEndian<quint16>(b.constData()+p); }
QByteArray inflate(const QByteArray& b, quint64 p, quint32 packed, quint32 expanded) {
    if (!expanded || expanded > 32*1024*1024) throw std::runtime_error("Asset expansion exceeds 32 MiB");
    range(b,p,packed);
    QByteArray framed(4,Qt::Uninitialized);
    qToBigEndian(expanded,framed.data());
    framed.append(b.constData()+p,packed);
    auto result=qUncompress(framed);
    if(result.size()!=expanded) throw std::runtime_error("Asset zlib length mismatch");
    return result;
}
// GIM TRUE-COLOUR HELPERS (palette entries and 16-bit pictures). The DXT path keeps
// its own 565 helper because it reads little-endian block payloads; these read the
// big-endian GIM picture/palette headers.
QRgb gimColour565(quint16 value) {
    const int r=value>>11,g=(value>>5)&63,b=value&31;
    return qRgb((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2));
}
QRgb gimColour5551(quint16 value) {
    const int r=(value>>10)&31,g=(value>>5)&31,b=value&31;
    return qRgba((r<<3)|(r>>2),(g<<3)|(g>>2),(b<<3)|(b>>2),value&0x8000?255:0);
}
QRgb gimColour4444(quint16 value) {
    return qRgba(((value>>12)&15)*17,((value>>8)&15)*17,((value>>4)&15)*17,(value&15)*17);
}
// The colour table of a GIM palette block (block type 0x05). The block repeats the
// image header: format at +0x04, order at +0x06, entry count as width*height at
// +0x08/+0x0A, and the first colour at the header's own pixel offset (+0x1C).
struct GimPalette {
    QVector<QRgb> colours;
    int format=-1;
};
bool readGimPalette(const QByteArray& b,quint64 begin,quint64 end,GimPalette& out) {
    for(quint64 block=begin;block+16<=end && block+16<=quint64(b.size());) {
        const auto type=u16(b,block);
        const auto size=u32(b,block+4),data=u32(b,block+12);
        if(size<16 || data<16 || data>=size || block+size>end || block+size>quint64(b.size())) {
            if(type==5)throw std::runtime_error("Invalid GIM palette block");
            return false;
        }
        if(type!=5) { block+=size;continue; }
        const quint64 info=block+data;
        range(b,info,0x30);
        const auto format=u16(b,info+4);
        const auto order=u16(b,info+6);
        const quint64 count=quint64(u16(b,info+8))*u16(b,info+10);
        // One entry is 2 bytes for the three 16-bit layouts and 4 for RGBA8888.
        const quint64 entryBytes=format<=2?2:format==3?4:0;
        if(!entryBytes)throw std::runtime_error(("Unsupported GIM palette format 0x"+QString::number(format,16)).toStdString());
        if(order!=0)throw std::runtime_error("Unsupported GIM palette order");
        // 16-bit indices cannot address more entries than this, and a palette with no
        // entries would make every lookup fail.
        if(!count || count>65536)throw std::runtime_error("Invalid GIM palette dimensions");
        const quint64 start=info+u32(b,info+0x1c);
        if(start<info+0x30 || start>block+size || count*entryBytes>block+size-start)throw std::runtime_error("GIM palette exceeds its block");
        range(b,start,count*entryBytes);
        out.format=format;
        out.colours.clear();
        out.colours.reserve(int(count));
        for(quint64 i=0;i<count;++i) {
            const quint64 at=start+i*entryBytes;
            if(format==3)out.colours.push_back(qRgba(uchar(b[int(at)]),uchar(b[int(at)+1]),uchar(b[int(at)+2]),uchar(b[int(at)+3])));
            else {
                const auto value=u16(b,at);
                out.colours.push_back(format==0?gimColour565(value):format==1?gimColour5551(value):gimColour4444(value));
            }
        }
        return true;
    }
    return false;
}
// INDEX4/INDEX8/INDEX16 pictures. The image header's pixel offset points at the index
// rows; each row is padded to the image's pitch alignment (16 bytes in the firmware
// 4.93 pictures), so row y starts at y*stride rather than y*rowBytes. When the picture
// carries no palette block the index is coverage: the PS3 4.93 OSK masks (LABEL_UP,
// EN_LABEL_*, MODE_GUIDE_*, KEYTOP_*, LIST_WINDOW_FOCUS) are format 0x5 with only a
// root/picture/image block and values that step through 0..255 as an anti-aliased
// alpha ramp, so they decode as white with the index as alpha and are tinted by the
// caller. Palette-bearing pictures map the index through the palette instead.
QImage decodeIndexed(const QByteArray& bytes,quint64 pixels,quint64 end,int width,int height,int format,quint32 alignment,const GimPalette& palette) {
    const quint64 bits=format==4?4:format==5?8:16;
    const quint64 rowBytes=(quint64(width)*bits+7)/8;
    const quint64 stride=((rowBytes+alignment-1)/alignment)*alignment;
    const quint64 length=stride*quint64(height);
    if(pixels>end || length>end-pixels)throw std::runtime_error("GIM index rows exceed image block");
    range(bytes,pixels,length);
    QImage result(width,height,QImage::Format_ARGB32);
    const int entries=int(palette.colours.size());
    for(int y=0;y<height;++y) {
        auto* row=reinterpret_cast<QRgb*>(result.scanLine(y));
        const auto* line=reinterpret_cast<const uchar*>(bytes.constData()+pixels+quint64(y)*stride);
        for(int x=0;x<width;++x) {
            quint32 index;
            if(format==4) { // high nibble is the left-hand pixel
                const auto packed=quint32(line[x>>1]);
                index=(x&1)?(packed&15):(packed>>4);
            } else if(format==5)index=line[x];
            else index=qFromBigEndian<quint16>(line+x*2);
            if(entries) {
                if(index>=quint32(entries))throw std::runtime_error(("GIM palette index "+QString::number(index)+" exceeds "+QString::number(entries)+" entries").toStdString());
                row[x]=palette.colours[int(index)];
            } else {
                const int coverage=format==4?int(index)*17:format==5?int(index):int(index>>8);
                row[x]=qRgba(255,255,255,coverage);
            }
        }
    }
    return result;
}
QImage decodeBlocks(const QByteArray& bytes,quint64 pixels,quint64 end,int width,int height,int format,quint32 alignment) {
    const quint64 blockBytes=format==8?8:16;
    const quint64 rowBytes=quint64((width+3)/4)*blockBytes;
    const quint64 stride=((rowBytes+alignment-1)/alignment)*alignment;
    const quint64 length=stride*((height+3)/4);
    if(pixels>end || length>end-pixels)throw std::runtime_error("DXT blocks exceed GIM image");
    range(bytes,pixels,length);
    QImage result(width,height,QImage::Format_ARGB32);
    const auto colour565=[](quint16 value) {
        const int r=value>>11,g=(value>>5)&63,b=value&31;
        return qRgb((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2));
    };
    for(int by=0;by<height;by+=4)for(int bx=0;bx<width;bx+=4) {
        const auto* block=reinterpret_cast<const uchar*>(bytes.constData()+pixels+(by/4)*stride+(bx/4)*blockBytes);
        const auto* colours=block+(format==8?0:8);
        // DXT payloads keep their standard little-endian block encoding even
        // inside these big-endian PS3 GIM containers.
        const quint16 c0=qFromLittleEndian<quint16>(colours),c1=qFromLittleEndian<quint16>(colours+2);
        std::array<QRgb,4> palette{colour565(c0),colour565(c1),0,0};
        if(format!=8 || c0>c1)for(int i=0;i<2;++i) {
            palette[2+i]=qRgb(((2-i)*qRed(palette[0])+(1+i)*qRed(palette[1]))/3,
                ((2-i)*qGreen(palette[0])+(1+i)*qGreen(palette[1]))/3,
                ((2-i)*qBlue(palette[0])+(1+i)*qBlue(palette[1]))/3);
        } else {palette[2]=qRgb((qRed(palette[0])+qRed(palette[1]))/2,(qGreen(palette[0])+qGreen(palette[1]))/2,(qBlue(palette[0])+qBlue(palette[1]))/2);palette[3]=qRgba(0,0,0,0);}
        const quint32 indices=qFromLittleEndian<quint32>(colours+4);
        std::array<int,8> alphas{};
        quint64 alphaIndices=0;
        if(format==10) {
            alphas[0]=block[0];alphas[1]=block[1];
            if(alphas[0]>alphas[1])for(int i=1;i<=6;++i)alphas[i+1]=((7-i)*alphas[0]+i*alphas[1])/7;
            else {for(int i=1;i<=4;++i)alphas[i+1]=((5-i)*alphas[0]+i*alphas[1])/5;alphas[6]=0;alphas[7]=255;}
            for(int i=0;i<6;++i)alphaIndices|=quint64(block[2+i])<<(i*8);
        }
        for(int y=0;y<4 && by+y<height;++y)for(int x=0;x<4 && bx+x<width;++x) {
            const int pixel=y*4+x;const auto colour=palette[(indices>>(pixel*2))&3];
            const int alpha=format==10?alphas[(alphaIndices>>(pixel*3))&7]:format==9?((block[pixel/2]>>((pixel&1)*4))&15)*17:qAlpha(colour);
            reinterpret_cast<QRgb*>(result.scanLine(by+y))[bx+x]=qRgba(qRed(colour),qGreen(colour),qBlue(colour),alpha);
        }
    }
    return result;
}
// ---- Same-artwork gate for the same-name upgrade (bestImage) ---------------------------------------
// "A larger picture with the same name" is not evidence of "the same artwork". explore_plugin_full:tex_music
// (46x46, the flat music note the Music column draws) shares its name with explore_category_sysconf:tex_music
// (128x128, the same note carrying the settings wrench) and xmb_ingame:tex_music (128x128, a glass note from
// the in-game icon set). The old policy took the largest same-aspect copy and therefore drew the settings art
// in the Music column.
//
// The policy is two gates of different kinds:
//   1. Intent: only names in sameArtNames() may be upgraded across containers. A name enters that list only
//      after a human compared the exported pictures; the signature below cannot see art direction (the glass
//      note's silhouette correlates 0.94 with the flat note, as high as some genuinely shared pictures).
//   2. Pixels: even for an allow-listed name, the two pictures must match after each is cropped to its own
//      alpha bounds and resampled to a small grid: the alpha silhouettes must correlate, overlap and keep
//      their bounding-box aspect, and the premultiplied colours must correlate. A future container that
//      reuses an allow-listed name for different art is therefore rejected with a logged reason.
// Both gates are scale-free, so a candidate can be any resolution above the source's.
constexpr int kArtGrid=32;
constexpr int kArtAlphaFloor=8;      // alpha at or below this is transparent padding, not artwork
constexpr double kArtAlphaCorrelation=0.80;
constexpr double kArtCoverage=0.75;   // IoU of the alpha silhouettes
constexpr double kArtColourCorrelation=0.80;
constexpr double kArtAspectTolerance=0.12;
const QStringList& sameArtNames() {
    // audit/TEXTURE-COVERAGE.md 2.1, re-checked against the exported pixels: the 46 px set in
    // explore_plugin_full and the 128 px set in xmb_ingame carry one filmstrip (tex_video), one camera
    // (tex_photo) and the PSN globe (tex_psn); xmb_plugin_normal:tex_psn is the same globe at 128 px.
    // tex_music is deliberately absent: its three containers hold three different pictures of the same
    // subject, and the Music column's own artwork is the flat note in explore_plugin_full.
    static const QStringList names{QStringLiteral("tex_psn"),QStringLiteral("tex_video"),QStringLiteral("tex_photo")};
    return names;
}
struct ArtSignature {
    bool valid=false;
    int width=0,height=0;
    double aspect=0;
    QVector<float> alpha,colour; // kArtGrid*kArtGrid, and 3*kArtGrid*kArtGrid premultiplied
};
// Alpha bounds + a fixed-size premultiplied sample of the visible picture. The bounds are the picture's own
// framing, so transparent padding and the source resolution do not affect the comparison.
ArtSignature artSignature(const QImage& image) {
    ArtSignature signature;
    if(image.isNull())return signature;
    const QImage source=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int left=source.width(),top=source.height(),right=-1,bottom=-1;
    for(int y=0;y<source.height();++y) {
        const auto* row=reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for(int x=0;x<source.width();++x) {
            if(qAlpha(row[x])<=kArtAlphaFloor)continue;
            left=std::min(left,x);right=std::max(right,x+1);
            top=std::min(top,y);bottom=std::max(bottom,y+1);
        }
    }
    if(right<=left || bottom<=top)return signature;
    signature.width=right-left;signature.height=bottom-top;
    signature.aspect=double(signature.width)/signature.height;
    const QImage tile=source.copy(QRect(left,top,signature.width,signature.height))
        .scaled(kArtGrid,kArtGrid,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    signature.alpha.reserve(kArtGrid*kArtGrid);
    signature.colour.reserve(3*kArtGrid*kArtGrid);
    for(int y=0;y<kArtGrid;++y) {
        const auto* row=reinterpret_cast<const QRgb*>(tile.constScanLine(y));
        for(int x=0;x<kArtGrid;++x) {
            const auto pixel=row[x];
            signature.alpha.push_back(float(qAlpha(pixel))/255.f);
            signature.colour.push_back(float(qRed(pixel))/255.f);
            signature.colour.push_back(float(qGreen(pixel))/255.f);
            signature.colour.push_back(float(qBlue(pixel))/255.f);
        }
    }
    signature.valid=signature.alpha.size()==kArtGrid*kArtGrid;
    return signature;
}
double correlation(const QVector<float>& a,const QVector<float>& b) {
    if(a.isEmpty() || a.size()!=b.size())return 0;
    double meanA=0,meanB=0;
    for(qsizetype i=0;i<a.size();++i){meanA+=a[i];meanB+=b[i];}
    meanA/=a.size();meanB/=b.size();
    double covariance=0,varianceA=0,varianceB=0;
    for(qsizetype i=0;i<a.size();++i) {
        const double da=a[i]-meanA,db=b[i]-meanB;
        covariance+=da*db;varianceA+=da*da;varianceB+=db*db;
    }
    // A flat picture (one colour, no variation) is equivalent to another flat picture of the same value.
    if(varianceA<1e-9 || varianceB<1e-9)return std::abs(meanA-meanB)<1e-6?1.0:0.0;
    return covariance/std::sqrt(varianceA*varianceB);
}
struct ArtMatch {
    bool equivalent=false;
    double alphaCorrelation=0,coverage=0,colourCorrelation=0,aspect=1;
    QString reason;
};
ArtMatch compareArtwork(const ArtSignature& base,const ArtSignature& other) {
    ArtMatch match;
    if(!base.valid || !other.valid) {match.reason=QStringLiteral("no visible alpha silhouette");return match;}
    match.aspect=base.aspect>0?other.aspect/base.aspect:0;
    match.alphaCorrelation=correlation(base.alpha,other.alpha);
    match.colourCorrelation=correlation(base.colour,other.colour);
    int intersection=0,joined=0;
    for(qsizetype i=0;i<base.alpha.size();++i) {
        const bool a=base.alpha[i]>0.1f,b=other.alpha[i]>0.1f;
        if(a && b)++intersection;
        if(a || b)++joined;
    }
    match.coverage=joined?double(intersection)/joined:0;
    if(std::abs(match.aspect-1.)>kArtAspectTolerance)
        match.reason=QStringLiteral("silhouette aspect ratio %1 vs %2").arg(match.aspect,0,'f',2).arg(1.0,0,'f',2);
    else if(match.alphaCorrelation<kArtAlphaCorrelation)
        match.reason=QStringLiteral("alpha correlation %1 < %2").arg(match.alphaCorrelation,0,'f',2).arg(kArtAlphaCorrelation,0,'f',2);
    else if(match.coverage<kArtCoverage)
        match.reason=QStringLiteral("silhouette overlap %1% < %2%").arg(match.coverage*100,0,'f',0).arg(kArtCoverage*100,0,'f',0);
    else if(match.colourCorrelation<kArtColourCorrelation)
        match.reason=QStringLiteral("colour correlation %1 < %2").arg(match.colourCorrelation,0,'f',2).arg(kArtColourCorrelation,0,'f',2);
    else match.equivalent=true;
    return match;
}
QString artMetrics(const ArtMatch& match) {
    return QStringLiteral("alpha corr %1, coverage IoU %2%, colour corr %3, aspect %4")
        .arg(match.alphaCorrelation,0,'f',2).arg(match.coverage*100,0,'f',0)
        .arg(match.colourCorrelation,0,'f',2).arg(match.aspect,0,'f',2);
}
// ---- Known container overrides ------------------------------------------------------------------------------
// The 4.93 RCO set reuses image names across containers, and a drawing site can ask for the container that
// looks right rather than the one that ships the picture. A rule is deliberately narrow: it names the
// requested pair, the container that actually contains that name, and the audit row that proves it. It
// fires only when the requested container has no such image (Assets::loadImage), so it can fill a null draw
// but can never replace a picture that already resolves. The table is the single place a wrong-container
// call site is migrated; no drawing site needs to change. audit/ICON-GAP-AUDIT.md section 9 lists it.
struct ContainerOverride { const char* requested; const char* name; const char* owner; const char* evidence; };
const ContainerOverride kContainerOverrides[] = {
    // The PS menu's controller row and the PS-tap plate asked system_plugin, which has never shipped a
    // controller glyph; impose_plugin does (impose_battery_padicon). ICON-GAP-AUDIT C1 is the visible gap.
    {"system_plugin","tex_controller","impose_plugin","ICON-GAP-AUDIT C1 (drawn: view.cpp:2130,2153)"},
    // The rest of the PS menu's controller panel family: the four slot numbers, the red slot plate, the
    // charge cell and its arrow. C7/B7 name the art; system_plugin has none of these six names.
    {"system_plugin","tex_controller_number","impose_plugin","ICON-GAP-AUDIT B7/C7"},
    {"system_plugin","tex_controller_shadow","impose_plugin","ICON-GAP-AUDIT C1 (shadow of the pad glyph)"},
    {"system_plugin","tex_red","impose_plugin","ICON-GAP-AUDIT B7/C7"},
    {"system_plugin","tex_battery","impose_plugin","ICON-GAP-AUDIT C7"},
    {"system_plugin","tex_battery_shadow","impose_plugin","ICON-GAP-AUDIT C7"},
    {"system_plugin","tex_charge_arrow","impose_plugin","ICON-GAP-AUDIT C7"},
    {"system_plugin","tex_charge_arrow_shadow","impose_plugin","ICON-GAP-AUDIT C7"},
    {"system_plugin","tex_error_batsu","impose_plugin","ICON-GAP-AUDIT C7"},
    {"system_plugin","tex_error_batsu_shadow","impose_plugin","ICON-GAP-AUDIT C7"},
};
} // namespace
Assets::Assets(QString flashRoot):root_(std::move(flashRoot)),font_("Segoe UI") {
    // Register every firmware font (all weights and coverage sets), keyed by file stem, e.g. "RD-B-LATIN".
    // Real bold/light/italic files then replace Qt's synthesised styles.
    QHash<QString,QString> families;
    const QDir fontDir(QDir(root_).filePath("data/font"));
    for(const auto& file:fontDir.entryList({"SCE-PS3-*.TTF"},QDir::Files,QDir::Name)) {
        // RD-L/R/B share one family name ("SCE-PS3 Rodin LATIN"); with the Light and italic faces registered, Qt's
        // weight matching picked Light for normal text, which read thin next to the reference. The XMB draws Regular
        // and Bold only.
        if(file.contains("-L-") || file.contains("-LI-") || file.contains("-I-") || file.contains("-BI-"))continue;
        const int id=QFontDatabase::addApplicationFont(fontDir.filePath(file));
        const auto names=id>=0?QFontDatabase::applicationFontFamilies(id):QStringList{};
        if(names.isEmpty()){qWarning().noquote()<<"assets: font not loaded"<<file;continue;}
        families.insert(QString(file).remove("SCE-PS3-").remove(".TTF"),names.first());
    }
    const auto family=[&](const QString& stem){return families.value(stem);};
    font_=family("RD-R-LATIN").isEmpty()?font_:family("RD-R-LATIN");
    fonts_.push_back(font_);
    for(const auto& stem:QStringList{"SR-R-LATIN","VR-R-LATIN"})fonts_.push_back(family(stem).isEmpty()?font_:family(stem));
    // Glyphs a Latin family lacks fall back to the firmware's own coverage sets in the order XMB needs them
    // (extended Latin, symbols, Japanese, Korean, Chinese), never to Windows fonts.
    const QStringList cjk{family("NR-R-JPN"),family("CP-R-KANA"),family("YG-R-KOR"),family("DH-R-CGB")};
    const QHash<QString,QStringList> chains{
        {"RD",{family("RD-R-LATIN2"),family("NR-R-EXT")}},
        {"SR",{family("SR-R-LATIN2"),family("SR-R-EXT"),family("SR-R-JPN")}},
        {"VR",{family("VR-R-LATIN2"),family("NR-R-EXT")}},
        {"MT",{family("RD-R-LATIN2"),family("NR-R-EXT")}},
    };
    for(auto it=chains.begin();it!=chains.end();++it) {
        const QString primary=family(it.key()+"-R-LATIN");
        if(primary.isEmpty())continue;
        QStringList substitutes;
        for(const auto& name:it.value()+cjk)if(!name.isEmpty() && name!=primary && !substitutes.contains(name))substitutes<<name;
        QFont::insertSubstitutions(primary,substitutes);
        qInfo().noquote()<<"assets: font"<<primary<<"falls back to"<<substitutes.join(", ");
    }
    qInfo()<<"assets: font styles"<<fonts_<<"("<<families.size()<<"firmware font files registered)";
    qInfo().noquote()<<"assets: local firmware"<<root_<<"font"<<font_;
}
Assets::Container Assets::readContainer(const QString& name) const {
    QFile source(QDir(root_).filePath("vsh/resource/"+name+".rco"));
    if(!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open local RCO");
    if(source.size()>64*1024*1024) throw std::runtime_error("RCO exceeds 64 MiB");
    Container c;c.file=source.readAll();
    if(u32(c.file,0)!=0x46525000) throw std::runtime_error("Expected PS3 big-endian RCO");
    const auto compression=u32(c.file,12)>>4;
    if(compression==0)c.tables=c.file;
    else if(compression==1)c.tables=c.file.left(0xa4)+inflate(c.file,0xb0,u32(c.file,0xa4),u32(c.file,0xa8));
    else throw std::runtime_error("Unsupported RCO header compression");
    c.labels=u32(c.file,0x40);c.imageData=u32(c.file,0x80);
    const quint32 labelLength=u32(c.file,0x44), root=u32(c.file,0x24);
    range(c.tables,c.labels,labelLength);
    if(root==0xffffffff)return c;
    const auto count=u32(c.tables,root+16);
    if(count>65536)throw std::runtime_error("RCO image count exceeds limit");
    quint64 at=quint64(root)+0x28;
    for(quint32 i=0;i<count;++i) {
        range(c.tables,at,0x38);
        if(u16(c.tables,at)!=0x0401)throw std::runtime_error("Unexpected RCO image entry");
        const auto label=u32(c.tables,at+4);
        if(label>=labelLength)throw std::runtime_error("RCO label is out of range");
        const auto end=c.tables.indexOf('\0',c.labels+label);
        if(end<0 || quint64(end)>=quint64(c.labels)+labelLength)throw std::runtime_error("Unterminated RCO label");
        c.entries.insert(QString::fromUtf8(c.tables.constData()+c.labels+label,end-c.labels-label),quint32(at));
        const auto next=u32(c.tables,at+20);
        if(i+1<count && next<0x38)throw std::runtime_error("Invalid RCO image sibling offset");
        at+=next;
    }
    qInfo().noquote()<<"assets: RCO"<<name<<c.entries.size()<<"named images";
    return c;
}
QImage Assets::decodeGim(const QByteArray& b,const QString& name) {
    if(!b.startsWith(".GIM1.00"))throw std::runtime_error("Unsupported GIM byte order");
    // Root and picture containers point to their first child; leaf blocks carry
    // their own total size. Only the first frame/mipmap is used for UI icons.
    // XMB_GIM_TRACE=1 prints the header, the palette and the result for every
    // decoded picture; --export-image is the read-only caller that writes the pixels.
    const bool trace=qEnvironmentVariableIntValue("XMB_GIM_TRACE")==1;
    const auto traceLine=[&](const QString& line) {
        if(trace)qInfo().noquote()<<"gim:"<<(name.isEmpty()?QStringLiteral("(unnamed)"):name)<<line;
    };
    quint64 at=0x10,pictureData=0,pictureEnd=quint64(b.size());
    for(int blocks=0;blocks<128;++blocks) {
        const auto type=u16(b,at);
        const auto size=u32(b,at+4),data=u32(b,at+12);
        if(size<16 || data<16 || data>=size)throw std::runtime_error("Invalid GIM block");
        range(b,at,size);
        if(type==2 || type==3) {
            // Remember the picture body so an indexed image can find its palette
            // among the picture's own children (normally the sibling after it).
            if(type==3){pictureData=at+data;pictureEnd=at+size;}
            at+=data;continue;
        }
        if(type!=4) { at+=size;continue; }
        const quint64 info=at+data;
        range(b,info,0x30);
        const auto format=u16(b,info+4),order=u16(b,info+6),w=u16(b,info+8),h=u16(b,info+10);
        const auto alignment=u16(b,info+14);
        if(order!=0)throw std::runtime_error("Unsupported GIM pixel order");
        if(!w || !h || w>4096 || h>4096 || !alignment || alignment>4096)throw std::runtime_error("Invalid GIM dimensions");
        const quint64 pixels=info+u32(b,info+0x1c);
        if(pixels<info+0x30)throw std::runtime_error("GIM pixels overlap image metadata");
        if(format>=8 && format<=10) {
            traceLine(QStringLiteral("format=0x%1 order=%2 %3x%4 align=%5 mode=block-compressed palette=-")
                          .arg(format,0,16).arg(order).arg(w).arg(h).arg(alignment));
            const QImage image=decodeBlocks(b,pixels,at+size,w,h,format,alignment);
            traceLine(QStringLiteral("result %1x%2").arg(image.width()).arg(image.height()));
            return image;
        }
        if(format>=4 && format<=6) {
            GimPalette palette;
            // The palette is a sibling of the image inside the same picture block.
            // Format 0x5 pictures in the 4.93 OSK RCO carry no palette block; those
            // bytes are an 8-bit coverage mask rather than palette indices.
            const bool found=readGimPalette(b,pictureData?pictureData:at+size,pictureEnd,palette);
            const QString paletteText=found
                ? QStringLiteral("%1 entries fmt=0x%2").arg(int(palette.colours.size())).arg(palette.format,0,16)
                : QStringLiteral("absent (coverage mask)");
            traceLine(QStringLiteral("format=0x%1 order=%2 %3x%4 align=%5 mode=index palette=%6")
                          .arg(format,0,16).arg(order).arg(w).arg(h).arg(alignment).arg(paletteText));
            const QImage image=decodeIndexed(b,pixels,at+size,w,h,format,alignment,palette);
            traceLine(QStringLiteral("result %1x%2").arg(image.width()).arg(image.height()));
            return image;
        }
        if(format!=3)throw std::runtime_error(("Unsupported GIM pixel format 0x"+QString::number(format,16)).toStdString());
        traceLine(QStringLiteral("format=0x3 order=%1 %2x%3 align=%4 mode=rgba8888 palette=-")
                      .arg(order).arg(w).arg(h).arg(alignment));
        const quint64 stride=((quint64(w)*4+alignment-1)/alignment)*alignment;
        if(pixels<info || pixels+stride*h>at+size)throw std::runtime_error("GIM pixels exceed image block");
        range(b,pixels,stride*h);
        QImage result(w,h,QImage::Format_ARGB32);
        for(int y=0;y<h;++y) {
            auto* row=reinterpret_cast<QRgb*>(result.scanLine(y));
            for(int x=0;x<w;++x) {
                const auto v=u32(b,pixels+y*stride+x*4);
                // PS3 GIM RGBA8888 pixels are R,G,B,A bytes. Transparent white
                // texels are FF FF FF 00, not an opaque cyan ABGR value.
                row[x]=qRgba(v>>24,(v>>16)&255,(v>>8)&255,v&255);
            }
        }
        traceLine(QStringLiteral("result %1x%2").arg(result.width()).arg(result.height()));
        return result;
    }
    throw std::runtime_error("GIM image block not found");
}
QImage Assets::decodeGimImage(const QByteArray& bytes) {
    try { return decodeGim(bytes); }
    catch(const std::exception& e) {
        qWarning().noquote()<<"assets: GIM picture skipped -"<<e.what();
        return {};
    }
}
const QImage& Assets::image(const QString& container,const QString& name) {
    return loadImage(container,name,nullptr);
}
const QImage& Assets::iconImage(const QString& container,const QString& name,ImageHit* hit) {
    return loadImage(container,name,hit);
}
Assets::ImageHit Assets::hitFor(const QString& container,const QString& name) const {
    ImageHit hit;hit.requestedContainer=container;hit.container=container;hit.name=name;
    if(container=="file" || container.startsWith("qgl/"))return hit;
    const auto requested=containers_.constFind(container);
    if(requested!=containers_.cend() && requested->entries.contains(name))return hit;
    const QString owner=knownOwner(container,name);
    if(!owner.isEmpty()) {
        const auto candidate=containers_.constFind(owner);
        if(candidate!=containers_.cend() && candidate->entries.contains(name)) {
            hit.container=owner;hit.migrated=true;hit.knownOwner=true;
        }
    }
    return hit;
}
const QImage& Assets::loadImage(const QString& container,const QString& name,ImageHit* hit) {
    const QString key=container+"/"+name;
    if(const auto it=images_.find(key);it!=images_.end()) {
        if(hit)*hit=hitFor(container,name);
        return it->second;
    }
    QImage image;
    try {
        if(container=="file") { // installed content (dev_hdd0 ICON0.PNG etc.), by absolute path
            image=QImage(name);
            if(image.isNull())throw std::runtime_error("image file could not be read");
            ++decodedImages_;
            if(hit)*hit=hitFor(container,name);
            return images_.insert_or_assign(key,std::move(image)).first->second;
        }
        if(container.startsWith("qgl/")) {
            image=decodeDds(qglFile(container.mid(4),name));
            // QGL icon maps use the original bottom-up texture coordinates.
            // Normalize them once for QPainter's top-down image convention.
            // Other DDS resources and RCO/GIM artwork have separate layouts.
            if(container=="qgl/icontex")image=image.transformed(QTransform::fromScale(1,-1));
            if(image.isNull())throw std::runtime_error("DDS decoder returned no pixels");
            ++decodedImages_;
            qInfo().noquote()<<"assets: decoded"<<key<<image.size();
            if(hit)*hit=hitFor(container,name);
            return images_.insert_or_assign(key,std::move(image)).first->second;
        }
        if(!containers_.contains(container))containers_.insert(container,readContainer(container));
        // The requested container is authoritative. A rule in kContainerOverrides is consulted only when
        // that container has no such name, so the migration can fill an empty draw but never changes one.
        QString source=container;
        if(!containers_[container].entries.contains(name)) {
            const QString owner=knownOwner(container,name);
            if(owner.isEmpty())
                throw std::runtime_error("Named image is absent from RCO and no container-override rule names an owner");
            if(!containers_.contains(owner))containers_.insert(owner,readContainer(owner));
            if(!containers_[owner].entries.contains(name))
                throw std::runtime_error(("Named image is absent from RCO; override owner "+owner+" does not ship it either").toStdString());
            qWarning().noquote()<<QStringLiteral("assets: %1 is absent from %2 - using known owner %3 (container override)")
                .arg(key,container,owner);
            source=owner;
        }
        const auto& c=containers_[source];
        const quint64 extra=quint64(c.entries[name])+0x28;
        const auto format=u16(c.tables,extra);
        const auto compression=u16(c.tables,extra+2)&255;
        const auto packed=u32(c.tables,extra+4),offset=u32(c.tables,extra+8);
        const quint64 start=quint64(c.imageData)+offset;
        range(c.file,start,packed);
        QByteArray data;
        if(compression==1)data=inflate(c.file,start,packed,u32(c.tables,extra+16));
        else if(compression==0)data=c.file.mid(start,packed);
        else throw std::runtime_error("Unsupported RCO image compression");
        image=format==5?decodeGim(data,name):QImage::fromData(data);
        if(image.isNull())throw std::runtime_error("Image decoder returned no pixels");
        ++decodedImages_;
        qInfo().noquote()<<"assets: decoded"<<key<<image.size();
    } catch(const std::exception& e) { qWarning().noquote()<<"assets:"<<key<<e.what(); }
    if(hit)*hit=hitFor(container,name);
    return images_.insert_or_assign(key,std::move(image)).first->second;
}
const QImage& Assets::bestImage(const QString& container,const QString& name) {
    const QImage& base=image(container,name);
    const QString key=container+"/"+name;
    // A picture image() resolved through a container-override rule is authoritative: the owner was chosen by
    // name+container, so no "largest same-name copy" search is needed (and no 122-container scan on first draw).
    const auto requestedContainer=containers_.constFind(container);
    const bool overrideResolved=!base.isNull() && !container.startsWith("qgl/") && container!="file"
        && requestedContainer!=containers_.cend() && !requestedContainer->entries.contains(name)
        && !knownOwner(container,name).isEmpty();
    if(container.startsWith("qgl/") || container=="file" || overrideResolved
       || (!base.isNull() && std::max(base.width(),base.height())>=96)) {
        BestDecision decision;
        decision.requestedContainer=container;decision.container=container;
        decision.allowListed=sameArtNames().contains(name);
        if(overrideResolved)
            decision.reason=QStringLiteral("resolved by container-override rule %1 -> %2 (no upgrade searched)")
                .arg(container,knownOwner(container,name));
        else decision.reason=base.isNull()?QStringLiteral("undecoded source with no larger copy")
            :QStringLiteral("source already at or above 96 px (no upgrade attempted)");
        decision.width=base.width();decision.height=base.height();
        bestDecisions_.insert(key,decision);
        return base;
    }
    if(const auto it=bestContainer_.constFind(key);it!=bestContainer_.cend()) {
        const QImage& cached=image(it.value(),name);
        BestDecision decision;
        decision.requestedContainer=container;decision.container=it.value();
        decision.allowListed=sameArtNames().contains(name);
        decision.upgraded=it.value()!=container;
        decision.gateApplied=decision.upgraded; // an upgrade only ever happens through the pixel gate
        decision.reason=QStringLiteral("cached choice");
        decision.width=cached.width();decision.height=cached.height();
        bestDecisions_.insert(key,decision);
        return cached;
    }
    ensureNameIndex();
    const QString traceSetting=qEnvironmentVariable("XMB_BESTIMAGE_TRACE");
    bool traceThis=false;
    if(!traceSetting.isEmpty()) {
        traceThis=traceSetting==QStringLiteral("1") || traceSetting.compare(QStringLiteral("all"),Qt::CaseInsensitive)==0;
        for(const auto& wanted:traceSetting.split(',')) {
            const QString item=wanted.trimmed();
            if(item==name || item==key)traceThis=true;
        }
    }
    const auto sizeText=[](const QImage& image){
        return image.isNull()?QStringLiteral("undecoded"):QStringLiteral("%1x%2").arg(image.width()).arg(image.height());
    };
    const bool allowListed=sameArtNames().contains(name);
    if(traceThis)
        qInfo().noquote()<<QStringLiteral("assets: bestImage: source %1 %2, same-art allow-list %3")
            .arg(key).arg(sizeText(base)).arg(allowListed?QStringLiteral("yes"):QStringLiteral("no"));
    QString best=container;
    qint64 bestArea=base.isNull()?0:qint64(base.width())*base.height();
    ArtMatch bestMatch;
    QStringList rejected;
    int largerCopies=0;
    for(const auto& candidate:nameIndex_.value(name)) {
        if(candidate==container)continue;
        const QImage& other=image(candidate,name);
        if(other.isNull())continue;
        const qint64 otherArea=qint64(other.width())*other.height();
        if(otherArea<=bestArea)continue; // only a higher resolution can replace the source
        ++largerCopies;
        // A source that did not decode has no artwork to compare: the old fallback to the largest copy stays,
        // because there is nothing to misrepresent. A decoded source is compared picture to picture.
        ArtMatch match;
        if(!base.isNull())match=compareArtwork(artSignature(base),artSignature(other));
        const bool takeCopy=base.isNull() || (allowListed && match.equivalent);
        QString reason;
        if(base.isNull())reason=QStringLiteral("source undecoded, largest same-name copy");
        else if(!allowListed)reason=QStringLiteral("name not in the same-art allow-list; ")+artMetrics(match);
        else if(!match.equivalent)reason=QStringLiteral("different artwork: ")+match.reason;
        if(traceThis) {
            qInfo().noquote()<<QStringLiteral("assets: bestImage: candidate %1/%2 %3 %4")
                .arg(candidate).arg(name).arg(sizeText(other))
                .arg(takeCopy?QStringLiteral("accept"):QStringLiteral("reject"));
            if(!base.isNull())
                qInfo().noquote()<<QStringLiteral("assets: bestImage: candidate metrics %1/%2 %3")
                    .arg(candidate).arg(name).arg(artMetrics(match));
        }
        if(!takeCopy) { rejected<<QStringLiteral("%1 %2 (%3)").arg(candidate).arg(sizeText(other)).arg(reason);continue; }
        best=candidate;bestArea=otherArea;bestMatch=match;
    }
    bestContainer_.insert(key,best);
    {
        BestDecision decision;
        decision.requestedContainer=container;decision.container=best;
        decision.allowListed=allowListed;
        decision.gateApplied=allowListed && !base.isNull() && largerCopies>0;
        decision.upgraded=best!=container;
        decision.width=image(best,name).width();decision.height=image(best,name).height();
        if(best!=container)
            decision.reason=base.isNull()?QStringLiteral("source undecoded; largest same-name copy")
                :QStringLiteral("same artwork confirmed (pixel gate passed)");
        else if(allowListed)
            decision.reason=largerCopies?QStringLiteral("allow-listed, pixel gate rejected every larger copy")
                :QStringLiteral("allow-listed, no larger same-name copy");
        else
            decision.reason=largerCopies?QStringLiteral("not allow-listed; larger same-name copies rejected")
                :QStringLiteral("no larger same-name copy");
        bestDecisions_.insert(key,decision);
    }
    if(best!=container) {
        const QString reason=base.isNull()?QStringLiteral("source undecoded, largest same-name copy")
            :QStringLiteral("same artwork confirmed (%1)").arg(artMetrics(bestMatch));
        qInfo().noquote()<<QStringLiteral("assets: bestImage: %1 %2 -> using %3 %4, %5")
            .arg(key).arg(sizeText(base)).arg(best).arg(sizeText(image(best,name))).arg(reason);
    } else if(largerCopies>0 && !base.isNull()) {
        const QString reason=allowListed?QStringLiteral("no equivalent candidate")
            :QStringLiteral("name not in the same-art allow-list");
        qInfo().noquote()<<QStringLiteral("assets: bestImage: %1 %2 keeping source (%3; %4 larger same-name copies rejected: %5)")
            .arg(key).arg(sizeText(base)).arg(reason).arg(largerCopies).arg(rejected.value(0));
    } else if(traceThis) {
        qInfo().noquote()<<QStringLiteral("assets: bestImage: %1 %2 keeping source (%3)")
            .arg(key).arg(sizeText(base))
            .arg(largerCopies?QStringLiteral("no equivalent copy"):QStringLiteral("no larger same-name copy"));
    }
    return image(best,name);
}
QString Assets::knownOwner(const QString& container,const QString& name) const {
    for(const auto& rule:kContainerOverrides)
        if(container==QLatin1String(rule.requested) && name==QLatin1String(rule.name))
            return QString::fromLatin1(rule.owner);
    return {};
}
QStringList Assets::sameArtAllowList() const { return sameArtNames(); }
QStringList Assets::containerOverrideRules() const {
    QStringList rules;
    for(const auto& rule:kContainerOverrides)
        rules<<QStringLiteral("%1/%2 -> %3 (%4)")
            .arg(QString::fromLatin1(rule.requested),QString::fromLatin1(rule.name),
                 QString::fromLatin1(rule.owner),QString::fromLatin1(rule.evidence));
    return rules;
}
void Assets::ensureNameIndex() {
    if(!nameIndex_.isEmpty())return;
    for(const auto& file:QDir(QDir(root_).filePath("vsh/resource")).entryInfoList({"*.rco"},QDir::Files,QDir::Name)) {
        try { for(const auto& entry:readContainer(file.completeBaseName()).entries.keys())nameIndex_[entry].push_back(file.completeBaseName()); }
        catch(const std::exception&) {}
    }
}
QStringList Assets::imageContainers(const QString& name) {
    ensureNameIndex();
    QStringList owners=nameIndex_.value(name);
    owners.sort();
    return owners;
}
Assets::BestDecision Assets::bestImageDecision(const QString& container,const QString& name) const {
    return bestDecisions_.value(container+"/"+name);
}
QByteArray Assets::qglFile(const QString& archive,const QString& name) {
    if(!qrcs_.contains(archive)) {
        auto qrc=std::make_shared<QrcArchive>(QDir(root_).filePath("vsh/resource/qgl/"+archive+".qrc"));
        qInfo().noquote()<<"assets: QRC"<<archive<<qrc->names().size()<<"named files";
        qrcs_.insert(archive,std::move(qrc));
    }
    return qrcs_[archive]->file(name);
}
QByteArray Assets::vshModule(const QString& name) const {
    if(modulesRoot_.isEmpty())return {};
    QFile file(QDir(modulesRoot_).filePath(name));
    return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();
}
QImage Assets::decodeDds(const QByteArray& bytes) {
    range(bytes,0,128);
    const auto le=[&](int at){return qFromLittleEndian<quint32>(bytes.constData()+at);};
    if(!bytes.startsWith("DDS ") || le(4)!=124 || le(76)!=32)throw std::runtime_error("Invalid DDS header");
    const auto w=le(16),h=le(12),flags=le(80),fourcc=le(84);
    if(!w || !h || w>4096 || h>4096)throw std::runtime_error("Invalid DDS dimensions");
    if(flags&4) {
        const int format=fourcc==0x31545844?8:fourcc==0x33545844?9:fourcc==0x35545844?10:0;
        if(!format)throw std::runtime_error("Unsupported DDS compression");
        return decodeBlocks(bytes,128,bytes.size(),w,h,format,1);
    }
    if(!(flags&0x40) || le(88)!=32)throw std::runtime_error("Unsupported DDS pixel format");
    const quint64 stride=(le(8)&8)?le(20):quint64(w)*4;
    if(stride<quint64(w)*4)throw std::runtime_error("Invalid DDS row pitch");
    range(bytes,128,stride*h);
    const quint32 masks[]={le(92),le(96),le(100),le(104)};
    struct Channel {quint32 mask;int shift;quint32 maximum;};
    std::array<Channel,4> channels{};
    quint32 used=0;
    for(int i=0;i<4;++i) {
        const auto mask=masks[i];int shift=0;
        if(!mask){if(i<3)throw std::runtime_error("Missing DDS colour mask");continue;}
        if(used&mask)throw std::runtime_error("Overlapping DDS colour masks");used|=mask;
        while(!((mask>>shift)&1))++shift;
        const auto maximum=mask>>shift;
        if(maximum&(maximum+1))throw std::runtime_error("Non-contiguous DDS colour mask");
        channels[i]={mask,shift,maximum};
    }
    QImage result(w,h,QImage::Format_ARGB32);
    for(quint32 y=0;y<h;++y)for(quint32 x=0;x<w;++x) {
        const auto pixel=qFromLittleEndian<quint32>(bytes.constData()+128+y*stride+x*4);
        int v[4]={0,0,0,255};
        for(int c=0;c<4;++c)if(channels[c].mask)v[c]=int(quint64((pixel&channels[c].mask)>>channels[c].shift)*255/channels[c].maximum);
        reinterpret_cast<QRgb*>(result.scanLine(y))[x]=qRgba(v[0],v[1],v[2],v[3]);
    }
    return result;
}
QHash<QString,QString> Assets::readEnglish(const Container& c) const {
    QHash<QString,QString> result;
    const auto root=u32(c.file,0x18),textData=u32(c.file,0x38),textLength=u32(c.file,0x3c);
    if(root==0xffffffff || textData==0xffffffff || !textLength)return result;
    range(c.file,textData,textLength);
    QByteArray english;
    const bool compressed=(u32(c.file,12)>>4)!=0;
    if(compressed) {
        quint64 chunk=textData;
        for(int i=0;i<64;++i) {
            range(c.file,chunk,16);
            if(chunk<textData || chunk+16>quint64(textData)+textLength)throw std::runtime_error("RCO language header exceeds text section");
            const auto lang=u16(c.file,chunk);
            const auto next=u32(c.file,chunk+4),packed=u32(c.file,chunk+8);
            if(packed>quint64(textData)+textLength-chunk-16)throw std::runtime_error("RCO language data exceeds text section");
            if(lang==1){english=inflate(c.file,chunk+16,packed,u32(c.file,chunk+12));break;}
            if(!next)break;
            if(next<16+quint64(packed))throw std::runtime_error("Invalid RCO language sibling offset");
            chunk+=next;
        }
    } else english=c.file.mid(textData,textLength);
    if(english.isEmpty())return result;
    const auto languages=u32(c.tables,root+16);
    if(languages>64)throw std::runtime_error("Invalid RCO language count");
    quint64 at=quint64(root)+0x28;
    for(quint32 language=0;language<languages;++language) {
        range(c.tables,at,0x30);
        const quint64 info=at+0x28;
        if(u16(c.tables,info)==1) {
            const auto format=u16(c.tables,info+2);const auto count=u32(c.tables,info+4);
            if(count>65536)throw std::runtime_error("Invalid RCO text count");
            range(c.tables,info+8,quint64(count)*12);
            for(quint32 i=0;i<count;++i) {
                const quint64 index=info+8+quint64(i)*12;
                const auto label=u32(c.tables,index),length=u32(c.tables,index+4),offset=u32(c.tables,index+8);
                if(label>=u32(c.file,0x44))throw std::runtime_error("RCO text label exceeds label section");
                const quint64 labelAt=quint64(c.labels)+label;
                const auto end=c.tables.indexOf('\0',labelAt);
                if(end<0 || quint64(end)>=quint64(c.labels)+u32(c.file,0x44))throw std::runtime_error("Unterminated RCO text label");
                range(english,offset,length);
                const QByteArrayView bytes(english.constData()+offset,length);
                QString value;
                if(format==0)value=QString::fromUtf8(bytes);
                else if(format==1){QStringDecoder decoder(QStringDecoder::Utf16BE);value=decoder(bytes);if(decoder.hasError())throw std::runtime_error("Invalid RCO UTF-16 text");}
                else if(format==2){QStringDecoder decoder(QStringDecoder::Utf32BE);value=decoder(bytes);if(decoder.hasError())throw std::runtime_error("Invalid RCO UTF-32 text");}
                else throw std::runtime_error("Unsupported RCO text encoding");
                while(value.endsWith(QChar(0)))value.chop(1);
                result.insert(QString::fromUtf8(c.tables.constData()+labelAt,end-labelAt),value);
            }
            return result;
        }
        const auto next=u32(c.tables,at+20);if(!next)break;
        if(next<0x30)throw std::runtime_error("Invalid RCO text sibling offset");
        at+=next;
    }
    return result;
}
QString Assets::text(const QString& container,const QString& name,const QString& fallback) {
    if(!texts_.contains(container)) {
        QHash<QString,QString> strings;
        try {
            if(!containers_.contains(container))containers_.insert(container,readContainer(container));
            strings=readEnglish(containers_[container]);
            qInfo().noquote()<<"assets: English strings"<<container<<strings.size();
        } catch(const std::exception& e){qWarning().noquote()<<"assets: text"<<container<<e.what();}
        texts_.insert(container,std::move(strings));
    }
    return texts_[container].value(name,fallback);
}
QImage Assets::decodeTga(const QByteArray& file) {
    if(file.size()<18)return {};
    const auto* d=reinterpret_cast<const uchar*>(file.constData());
    const int idLength=d[0],mapType=d[1],type=d[2],width=d[12]|(d[13]<<8),height=d[14]|(d[15]<<8),bpp=d[16],descriptor=d[17];
    if(mapType!=0 || !(type==2||type==3||type==10||type==11) || width<=0 || height<=0 || !(bpp==8||bpp==24||bpp==32))return {};
    const int bytes=bpp/8;qsizetype at=18+idLength;
    QImage image(width,height,QImage::Format_ARGB32);
    const auto pixel=[&](const uchar* p){return bytes==1?qRgb(p[0],p[0],p[0]):qRgba(p[2],p[1],p[0],bytes==4?p[3]:255);};
    std::vector<QRgb> pixels;pixels.reserve(size_t(width)*height);
    while(pixels.size()<size_t(width)*height) {
        if(type<9) { if(at+bytes>file.size())return {}; pixels.push_back(pixel(d+at));at+=bytes;continue; }
        if(at>=file.size())return {};
        const int header=d[at++],count=(header&0x7f)+1;
        if(header&0x80) { if(at+bytes>file.size())return {}; const QRgb p=pixel(d+at);at+=bytes;for(int i=0;i<count;++i)pixels.push_back(p); }
        else for(int i=0;i<count;++i) { if(at+bytes>file.size())return {}; pixels.push_back(pixel(d+at));at+=bytes; }
    }
    const bool topFirst=descriptor&0x20;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)image.setPixel(x,topFirst?y:height-1-y,pixels[size_t(y)*width+x]);
    return image;
}
}
