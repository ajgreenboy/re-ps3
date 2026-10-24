#include "assets.h"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QtEndian>
#include <stdexcept>
#include <limits>
#include <array>
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
}
Assets::Assets(QString flashRoot):root_(std::move(flashRoot)),font_("Segoe UI") {
    const QString path=QDir(root_).filePath("data/font/SCE-PS3-RD-R-LATIN.TTF");
    const int id=QFontDatabase::addApplicationFont(path);
    if(id>=0 && !QFontDatabase::applicationFontFamilies(id).isEmpty()) font_=QFontDatabase::applicationFontFamilies(id).first();
    fonts_.push_back(font_);
    for(const auto& name:QStringList{"SCE-PS3-SR-R-LATIN.TTF","SCE-PS3-VR-R-LATIN.TTF"}) {
        const int extra=QFontDatabase::addApplicationFont(QDir(root_).filePath("data/font/"+name));
        fonts_.push_back(extra>=0 && !QFontDatabase::applicationFontFamilies(extra).isEmpty()?QFontDatabase::applicationFontFamilies(extra).first():font_);
    }
    qInfo()<<"assets: font styles"<<fonts_;
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
QImage Assets::decodeGim(const QByteArray& b) {
    if(!b.startsWith(".GIM1.00"))throw std::runtime_error("Unsupported GIM byte order");
    // Root and picture containers point to their first child; leaf blocks carry
    // their own total size. Only the first frame/mipmap is used for UI icons.
    quint64 at=0x10;
    for(int blocks=0;blocks<128;++blocks) {
        const auto type=u16(b,at);
        const auto size=u32(b,at+4),data=u32(b,at+12);
        if(size<16 || data<16 || data>=size)throw std::runtime_error("Invalid GIM block");
        range(b,at,size);
        if(type==2 || type==3) { at+=data;continue; }
        if(type!=4) { at+=size;continue; }
        const quint64 info=at+data;
        range(b,info,0x30);
        const auto format=u16(b,info+4),order=u16(b,info+6),w=u16(b,info+8),h=u16(b,info+10);
        const auto alignment=u16(b,info+14);
        if(order!=0)throw std::runtime_error("Unsupported GIM pixel order");
        if(!w || !h || w>4096 || h>4096 || !alignment || alignment>4096)throw std::runtime_error("Invalid GIM dimensions");
        const quint64 pixels=info+u32(b,info+0x1c);
        if(pixels<info+0x30)throw std::runtime_error("GIM pixels overlap image metadata");
        if(format>=8 && format<=10)return decodeBlocks(b,pixels,at+size,w,h,format,alignment);
        if(format!=3)throw std::runtime_error(("Unsupported GIM pixel format 0x"+QString::number(format,16)).toStdString());
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
        return result;
    }
    throw std::runtime_error("GIM image block not found");
}
const QImage& Assets::image(const QString& container,const QString& name) {
    const QString key=container+"/"+name;
    if(auto it=images_.constFind(key);it!=images_.cend())return it.value();
    QImage image;
    try {
        if(container.startsWith("qgl/")) {
            image=decodeDds(qglFile(container.mid(4),name));
            // QGL icon maps use the original bottom-up texture coordinates.
            // Normalize them once for QPainter's top-down image convention.
            // Other DDS resources and RCO/GIM artwork have separate layouts.
            if(container=="qgl/icontex")image=image.transformed(QTransform::fromScale(1,-1));
            if(image.isNull())throw std::runtime_error("DDS decoder returned no pixels");
            ++decodedImages_;
            qInfo().noquote()<<"assets: decoded"<<key<<image.size();
            return images_.insert(key,std::move(image)).value();
        }
        if(!containers_.contains(container))containers_.insert(container,readContainer(container));
        const auto& c=containers_[container];
        if(!c.entries.contains(name))throw std::runtime_error("Named image is absent from RCO");
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
        image=format==5?decodeGim(data):QImage::fromData(data);
        if(image.isNull())throw std::runtime_error("Image decoder returned no pixels");
        ++decodedImages_;
        qInfo().noquote()<<"assets: decoded"<<key<<image.size();
    } catch(const std::exception& e) { qWarning().noquote()<<"assets:"<<key<<e.what(); }
    return images_.insert(key,std::move(image)).value();
}
QByteArray Assets::qglFile(const QString& archive,const QString& name) {
    if(!qrcs_.contains(archive)) {
        auto qrc=std::make_shared<QrcArchive>(QDir(root_).filePath("vsh/resource/qgl/"+archive+".qrc"));
        qInfo().noquote()<<"assets: QRC"<<archive<<qrc->names().size()<<"named files";
        qrcs_.insert(archive,std::move(qrc));
    }
    return qrcs_[archive]->file(name);
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
}
