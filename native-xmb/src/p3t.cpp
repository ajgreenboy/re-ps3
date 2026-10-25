#include "p3t.h"
#include <QFile>
#include <QSet>
#include <QtEndian>
#include <bit>
#include <algorithm>
#include <cmath>

namespace xmb {
namespace {
constexpr quint32 kNone=0xFFFFFFFFu;
quint32 be32(const QByteArray& d,qsizetype at){return at>=0 && at+4<=d.size()?qFromBigEndian<quint32>(d.constData()+at):kNone;}
// Python-style slice: stops at the end of the data.
QByteArray clip(const QByteArray& d,qsizetype a,qsizetype b){a=std::clamp<qsizetype>(a,0,d.size());return d.mid(a,std::clamp<qsizetype>(b,a,d.size())-a);}
QString cstr(const QByteArray& d,qsizetype at) {
    if(at<0 || at>=d.size())return {};
    const qsizetype end=d.indexOf('\0',at);
    return QString::fromUtf8(d.constData()+at,(end<0?d.size():end)-at);
}
// zlib stream without the size header qUncompress expects; the expanded size is not stored, so grow the hint.
QByteArray inflate(const QByteArray& packed) {
    if(packed.size()<2 || (quint8(packed[0])&0x0F)!=8 || ((quint8(packed[0])<<8)|quint8(packed[1]))%31)return packed;
    for(quint64 hint=std::max<quint64>(quint64(packed.size())*4,1<<16);hint<=(256u<<20);hint*=4) {
        QByteArray framed(4,'\0');qToBigEndian<quint32>(quint32(hint),framed.data());framed+=packed;
        auto out=qUncompress(framed);
        if(!out.isEmpty())return out;
    }
    return packed;
}
// PSP "faststorage" order: 16-byte x 8-row tiles.
QByteArray unswizzle(const QByteArray& in,qsizetype pitch,qsizetype rows) {
    QByteArray out(in.size(),'\0');qsizetype src=0;
    for(qsizetype ty=0;ty<rows;ty+=8)for(qsizetype tx=0;tx<pitch;tx+=16)for(qsizetype y=ty;y<std::min(ty+8,rows);++y) {
        const qsizetype d=y*pitch+tx,n=std::min({qsizetype(16),out.size()-d,in.size()-src});
        if(n>0)std::copy_n(in.constData()+src,n,out.data()+d);
        src+=16;
    }
    return out;
}
// GIM pixel formats 0..3 (RGB565, RGBA5551, RGBA4444, RGBA8888) to RGBA8888.
QByteArray expand(int format,const QByteArray& px,qsizetype count,bool big) {
    QByteArray out(4*count,'\0');
    if(format==3){std::copy_n(px.constData(),std::min(px.size(),out.size()),out.data());return out;}
    for(qsizetype i=0;i<count && 2*i+2<=px.size();++i) {
        const auto* b=reinterpret_cast<const uchar*>(px.constData()+2*i);
        const quint32 v=big?(b[0]<<8|b[1]):(b[1]<<8|b[0]);
        quint32 r,g,bl,a;
        if(format==0){r=(v&31)*255/31;g=((v>>5)&63)*255/63;bl=((v>>11)&31)*255/31;a=255;}
        else if(format==1){r=(v&31)*255/31;g=((v>>5)&31)*255/31;bl=((v>>10)&31)*255/31;a=v>>15?255:0;}
        else {r=(v&15)*17;g=((v>>4)&15)*17;bl=((v>>8)&15)*17;a=(v>>12)*17;}
        out[4*i]=char(r);out[4*i+1]=char(g);out[4*i+2]=char(bl);out[4*i+3]=char(a);
    }
    return out;
}
}

QString P3t::Element::text(const QString& key) const {
    const auto found=attributes.constFind(key);
    return found!=attributes.cend() && found->kind==Attribute::Kind::String?found->text:QString{};
}

P3t P3t::read(QByteArray data,QString* error) {
    const auto fail=[&](const QString& why){if(error)*error=why;return P3t{};};
    if(data.size()<56 || (!data.startsWith("P3TF") && !data.startsWith("RAFO")))return fail("This is not a PS3 theme file.");
    quint32 h[13];for(int i=0;i<13;++i)h[i]=be32(data,4+4*i);
    P3t theme;
    const QByteArray tree=clip(data,h[1],qsizetype(h[1])+h[2]),ids=clip(data,h[3],qsizetype(h[3])+h[4]),strings=clip(data,h[5],qsizetype(h[5])+h[6]);
    const QByteArray intBytes=clip(data,h[7],qsizetype(h[7])+h[8]-h[8]%4),floatBytes=clip(data,h[9],qsizetype(h[9])+h[10]-h[10]%4);
    theme.files_=clip(data,h[11],qsizetype(h[11])+h[12]);
    QVector<qint64> ints;QVector<double> floats;
    for(qsizetype i=0;i+4<=intBytes.size();i+=4)ints.push_back(qint32(be32(intBytes,i)));
    for(qsizetype i=0;i+4<=floatBytes.size();i+=4)floats.push_back(std::bit_cast<float>(be32(floatBytes,i)));
    // Element: name, attribute count, parent, previous, next sibling, first child, last child (28 bytes), then
    // 16-byte attributes {name, kind, value, value}. Walked depth-first; a node seen twice ends that branch.
    QVector<QPair<quint32,int>> stack{{0,0}};QSet<quint32> seen;
    while(!stack.isEmpty()) {
        const auto [off,depth]=stack.takeLast();
        if(qsizetype(off)+28>tree.size() || seen.contains(off) || seen.size()>200000)continue;
        seen.insert(off);
        const auto w=[&](int i){return be32(tree,qsizetype(off)+4*i);};
        Element element{depth,cstr(strings,w(0)),{}};
        for(quint32 i=0;i<w(1);++i) {
            const qsizetype a=qsizetype(off)+28+16*qsizetype(i);
            if(a+16>tree.size())break;
            const quint32 kind=be32(tree,a+4),v1=be32(tree,a+8),v2=be32(tree,a+12);
            Attribute value;
            const auto range=[&](qsizetype n){return std::pair{std::min<qsizetype>(v1,n),std::min<qsizetype>(qsizetype(v1)+v2,n)};};
            switch(kind) {
            case 1:value.kind=Attribute::Kind::Int;value.integer=qint32(v1);break;
            case 2:value.kind=Attribute::Kind::Float;value.real=std::bit_cast<float>(v1);break;
            case 3:value.kind=Attribute::Kind::String;value.text=cstr(strings,v1);break;
            case 4:{value.kind=Attribute::Kind::Ints;const auto [b,e]=range(ints.size());value.integers=ints.mid(b,e-b);break;}
            case 5:{value.kind=Attribute::Kind::Floats;const auto [b,e]=range(floats.size());value.reals=floats.mid(b,e-b);break;}
            case 6:value.kind=Attribute::Kind::File;value.offset=v1;value.size=v2;break;
            case 7:case 8:value.kind=Attribute::Kind::String;value.text=cstr(ids,qsizetype(v1)+4);break; // after a 4-byte back reference
            default:continue;
            }
            element.attributes.insert(cstr(strings,be32(tree,a)),std::move(value));
        }
        theme.elements_.push_back(std::move(element));
        if(w(4)!=kNone)stack.push_back({w(4),depth});
        if(w(5)!=kNone)stack.push_back({w(5),depth+1});
    }
    if(theme.elements_.isEmpty())return fail("The theme file holds no elements.");
    theme.data_=std::move(data);
    return theme;
}
P3t P3t::load(const QString& path,QString* error) {
    QFile file(path);
    // Retail themes are a few MB to ~100 MB; the whole file stays in memory while the theme is read.
    if(!file.open(QIODevice::ReadOnly) || file.size()>512ll*1024*1024){if(error)*error="The theme file could not be read.";return {};}
    return read(file.readAll(),error);
}
QByteArray P3t::file(const Attribute& attribute) const {
    if(attribute.kind!=Attribute::Kind::File)return {};
    return inflate(clip(files_,attribute.offset,qsizetype(attribute.offset)+attribute.size));
}
QByteArray P3t::file(const Element& element,const QString& key) const {
    const auto found=element.attributes.constFind(key);
    return found==element.attributes.cend()?QByteArray{}:file(*found);
}
const P3t::Element* P3t::find(const QString& name) const {
    for(const auto& element:elements_)if(element.name==name)return &element;
    return nullptr;
}
QString P3t::name() const {const auto* info=find("info");return info?info->text("name"):QString{};}
QString P3t::author() const {const auto* info=find("info");return info?info->text("author"):QString{};}
bool P3t::dynamic() const {
    for(const auto& element:elements_)if(element.name=="bgimage" && element.attributes.contains("anim"))return true;
    return false;
}
QImage P3t::background() const {
    for(const auto& element:elements_) {
        if(element.name!="bgimage")continue;
        for(const QString& key:{QStringLiteral("hd"),QStringLiteral("sd")}) {
            const auto bytes=file(element,key);
            if(bytes.isEmpty())continue;
            auto image=QImage::fromData(bytes);
            if(!image.isNull())return image;
        }
    }
    return {};
}
QImage P3t::preview() const {const auto* info=find("info");return info?gim(file(*info,"preview")):QImage{};}
QHash<QString,QImage> P3t::icons() const {
    QHash<QString,QImage> result;
    for(const auto& element:elements_) {
        if(element.name!="icon")continue;
        const auto id=element.text("id");
        if(id.isEmpty() || result.contains(id))continue;
        auto image=gim(file(element,"src"));
        if(!image.isNull())result.insert(id,std::move(image)); // an icon that can't be read keeps the system's
    }
    return result;
}
QHash<QString,QByteArray> P3t::sounds() const {
    QHash<QString,QByteArray> result;
    for(const auto& element:elements_) {
        if(element.name!="se")continue;
        const auto id=element.text("id");
        if(id.isEmpty() || result.contains(id))continue;
        const auto mono=file(element,"src"),left=file(element,"left"),right=file(element,"right");
        auto wav=!mono.isEmpty()?vagToWav(mono):!left.isEmpty()?vagToWav(left,right):QByteArray{};
        if(!wav.isEmpty())result.insert(id,std::move(wav));
    }
    return result;
}

QImage P3t::gim(const QByteArray& g) {
    if(g.size()<16)return {};
    const bool big=g.startsWith(".GIM");
    if(!big && !g.startsWith("MIG."))return {};
    const auto r16=[&](qsizetype at)->qint64{if(at<0 || at+2>g.size())return -1;const auto* b=reinterpret_cast<const uchar*>(g.constData()+at);return big?(b[0]<<8|b[1]):(b[1]<<8|b[0]);};
    const auto r32=[&](qsizetype at)->qint64{if(at<0 || at+4>g.size())return -1;return big?qFromBigEndian<quint32>(g.constData()+at):qFromLittleEndian<quint32>(g.constData()+at);};
    qsizetype image=-1,palette=-1;
    QVector<QPair<qsizetype,qsizetype>> blocks{{16,g.size()}};int guard=0;
    while(!blocks.isEmpty()) {
        auto [off,end]=blocks.takeLast();
        while(off+16<=end) {
            if(++guard>10000)return {};
            const qint64 kind=r16(off),size=r32(off+4),dataAt=r32(off+12);
            if(kind<0 || size<0 || dataAt<0)break;
            if(kind==2 || kind==3)blocks.push_back({off+dataAt,std::min<qsizetype>(off+size,g.size())});
            else if(kind==4 && image<0)image=off+dataAt;
            else if(kind==5 && palette<0)palette=off+dataAt;
            if(size==0)break;
            off+=size;
        }
    }
    if(image<0)return {};
    struct Header{int format=0,order=0;qsizetype width=0,height=0,start=0,stop=0;};
    const auto header=[&](qsizetype at){return Header{int(r16(at+4)),int(r16(at+6)),qsizetype(r16(at+8)),qsizetype(r16(at+10)),at+qsizetype(r32(at+0x1C)),at+qsizetype(r32(at+0x20))};};
    const auto h=header(image);
    if(h.width<=0 || h.height<=0 || h.width>4096 || h.height>4096 || h.format<0 || h.format>5)return {};
    QByteArray data=clip(g,h.start,h.stop);
    const qsizetype pitch=data.size()/h.height;
    if(h.order==1)data=unswizzle(data,pitch,h.height);
    QImage out(int(h.width),int(h.height),QImage::Format_RGBA8888);
    if(h.format==4 || h.format==5) {
        if(palette<0)return {};
        const auto p=header(palette);
        const auto colours=expand(p.format,clip(g,p.start,p.stop),std::clamp<qsizetype>(p.width,0,256),big);
        for(qsizetype y=0;y<h.height;++y) {
            auto* row=out.scanLine(int(y));const qsizetype base=y*pitch;
            for(qsizetype x=0;x<h.width;++x) {
                const qsizetype at=base+(h.format==4?x>>1:x);
                const int byte=at<data.size()?quint8(data[at]):0;
                const qsizetype index=h.format==4?(byte>>(4*(x&1)))&15:byte;
                for(int c=0;c<4;++c)row[4*x+c]=4*index+c<colours.size()?uchar(colours[4*index+c]):0;
            }
        }
    } else {
        const qsizetype bpp=h.format==3?4:2;
        for(qsizetype y=0;y<h.height;++y) {
            const auto rgba=expand(h.format,clip(data,y*pitch,y*pitch+h.width*bpp),h.width,big);
            std::copy_n(rgba.constData(),rgba.size(),reinterpret_cast<char*>(out.scanLine(int(y))));
        }
    }
    return out;
}

QByteArray P3t::vagToWav(const QByteArray& left,const QByteArray& right) {
    const auto decode=[](const QByteArray& v,quint32& rate) {
        qsizetype p=0;rate=44100;
        if(v.startsWith("VAGp")){const quint32 r=be32(v,16);if(r && r!=kNone)rate=r;p=48;}
        static constexpr double k[5][2]{{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
        double s1=0,s2=0;QVector<qint16> out;
        for(;p+16<=v.size();p+=16) {
            const auto* b=reinterpret_cast<const uchar*>(v.constData()+p);
            if(b[1]==7)break; // end of the sound
            const int shift=std::min(b[0]&15,12);const auto& c=k[std::min(b[0]>>4,4)];
            for(int i=0;i<28;++i) {
                const int nibble=i%2?b[2+i/2]>>4:b[2+i/2]&15;
                const int raw=qint16(quint16(nibble<<12))>>4; // sign-extended, scaled to 12 bits
                const double x=std::clamp(std::round(double((raw<<4)>>shift)+(s1*c[0]+s2*c[1])/64.),-32768.,32767.);
                out.push_back(qint16(x));s2=s1;s1=x;
            }
            if(b[1]==1)break; // last block, no loop
        }
        return out;
    };
    quint32 rate=44100,unused=0;
    const auto l=decode(left,rate);
    if(l.isEmpty())return {};
    const auto r=right.isEmpty()?QVector<qint16>{}:decode(right,unused);
    const quint16 channels=r.isEmpty()?1:2;
    const qsizetype frames=r.isEmpty()?l.size():std::min(l.size(),r.size());
    const quint32 bytes=quint32(frames)*channels*2;
    QByteArray w;w.reserve(44+bytes);
    const auto u32=[&](quint32 v){char b[4];qToLittleEndian(v,b);w.append(b,4);};
    const auto u16=[&](quint16 v){char b[2];qToLittleEndian(v,b);w.append(b,2);};
    w+="RIFF";u32(36+bytes);w+="WAVEfmt ";u32(16);u16(1);u16(channels);u32(rate);u32(rate*channels*2);u16(channels*2);u16(16);
    w+="data";u32(bytes);
    for(qsizetype i=0;i<frames;++i){u16(quint16(l[i]));if(channels==2)u16(quint16(r[i]));}
    return w;
}
}
