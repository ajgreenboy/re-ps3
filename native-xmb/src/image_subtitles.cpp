#include "image_subtitles.h"
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace xmb {
namespace {
// Bounds. A feature-length PGS track is 5-40 MB and a VobSub .sub a few MB; anything bigger is a
// wrong file. The pixel budget is for the decoded timeline (Indexed8, one byte per pixel).
constexpr qint64 maximumInputBytes=64ll*1024*1024;
constexpr int maximumDimension=4096;
constexpr int maximumCues=20000;
constexpr qint64 maximumImageBytes=512ll*1024*1024;
constexpr qint64 maximumObjectBytes=256ll*1024*1024;   // decoded PGS objects alive at once
constexpr qint64 maximumPixelWork=1500ll*1000*1000;     // pixels decoded+composed per stream: bounds CPU time
constexpr int maximumPgsObjects=64;                     // the BD spec allows 64 per epoch
constexpr int maximumPgsWindows=64;                     // the spec allows 2; tolerate more
constexpr int maximumPgsCompositionObjects=64;
constexpr int maximumIdxEntries=200000;
constexpr int maximumSpuPackets=128;                    // 64 KB SPU / ~2 KB packs, with slack
constexpr qint64 defaultDurationMs=5000;                // a last cue with no clear/stop
constexpr qint64 maximumGapAsDurationMs=10000;          // VobSub with no stop: run to the next cue up to this

inline int be16(const uchar* p){return (int(p[0])<<8)|int(p[1]);}
inline int be24(const uchar* p){return (int(p[0])<<16)|(int(p[1])<<8)|int(p[2]);}
inline quint32 be32(const uchar* p){return (quint32(p[0])<<24)|(quint32(p[1])<<16)|(quint32(p[2])<<8)|quint32(p[3]);}

// Collects finished cues under the cue-count and pixel budgets.
struct Timeline {
    QVector<ImageCue>& out;
    qint64 bytes=0;
    bool full=false;
    int dropped=0;   // cues with end <= start
    explicit Timeline(QVector<ImageCue>& o):out(o){}
    bool add(ImageCue cue) {
        if(full)return false;
        if(cue.endMs<=cue.startMs){++dropped;return true;}
        const qint64 size=cue.image.sizeInBytes();
        if(out.size()>=maximumCues || bytes+size>maximumImageBytes){full=true;return false;}
        bytes+=size;out.push_back(std::move(cue));
        return true;
    }
};

// Trims an Indexed8 image to the bounding box of pixels whose table alpha is non-zero. Returns false
// when nothing is visible.
bool trimToVisible(QImage& image,QRect& rect) {
    const auto table=image.colorTable();
    std::array<bool,256> visible{};
    for(int i=0;i<table.size() && i<256;++i)visible[size_t(i)]=qAlpha(table[i])!=0;
    int left=image.width(),right=-1,top=image.height(),bottom=-1;
    for(int y=0;y<image.height();++y) {
        const uchar* line=image.constScanLine(y);
        int first=-1,last=-1;
        for(int x=0;x<image.width();++x)if(visible[line[x]]){first=x;break;}
        if(first<0)continue;
        for(int x=image.width()-1;x>=first;--x)if(visible[line[x]]){last=x;break;}
        left=std::min(left,first);right=std::max(right,last);
        top=std::min(top,y);bottom=y;
    }
    if(right<0)return false;
    const QRect box(left,top,right-left+1,bottom-top+1);
    if(box!=image.rect())image=image.copy(box);
    rect=QRect(rect.topLeft()+box.topLeft(),box.size());
    return true;
}

// ---------------------------------------------------------------------------------------------
// PGS
// ---------------------------------------------------------------------------------------------
struct PgsObject {
    int width=0,height=0;
    QByteArray rle;
    bool decoded=false;
    QByteArray pixels;   // width*height palette indices once decoded
};
struct PgsWindow { QRect rect; };
struct PgsCompositionObject {
    int objectId=0,windowId=0;
    bool cropped=false,forced=false;
    int x=0,y=0;
    QRect crop;
};
struct PgsComposition {
    qint64 ms=0;
    int width=0,height=0,state=0,paletteId=0;
    QVector<PgsCompositionObject> objects;
};
struct YCbCrA { uchar y=16,cr=128,cb=128,a=0; };
using PgsPalette=std::array<YCbCrA,256>;

// Studio-range YCbCr to 8-bit RGB. HD uses BT.709, SD BT.601 (what players and ffmpeg do, keyed on
// the composition height). Out-of-range codes are clamped, so a full-range encoder's palette comes
// out slightly stretched rather than wrapped.
QRgb pgsColour(const YCbCrA& e,bool hd) {
    if(e.a==0)return qRgba(0,0,0,0);
    const double y=(double(e.y)-16.0)*255.0/219.0;
    const double cb=(double(e.cb)-128.0)*255.0/224.0;
    const double cr=(double(e.cr)-128.0)*255.0/224.0;
    double r,g,b;
    if(hd){r=y+1.5748*cr;g=y-0.187324*cb-0.468124*cr;b=y+1.8556*cb;}
    else{r=y+1.402*cr;g=y-0.344136*cb-0.714136*cr;b=y+1.772*cb;}
    auto clamp=[](double v){return int(std::lround(std::clamp(v,0.0,255.0)));};
    return qRgba(clamp(r),clamp(g),clamp(b),e.a);
}

// HDMV RLE: a non-zero byte is one pixel; 00 00 ends the line; 00 then a flag byte encodes a run of
// colour 0 or of an explicit colour, with a 6- or 14-bit length. Runs past the line end are clipped.
void decodePgsObject(PgsObject& object) {
    object.decoded=true;
    const qint64 count=qint64(object.width)*object.height;
    object.pixels=QByteArray(qsizetype(count),'\xFF');
    auto* pixels=reinterpret_cast<uchar*>(object.pixels.data());
    const auto* data=reinterpret_cast<const uchar*>(object.rle.constData());
    const qsizetype size=object.rle.size();
    qsizetype i=0;int x=0,y=0;
    while(i<size && y<object.height) {
        int colour=0,run=1;
        const uchar first=data[i++];
        if(first) colour=first;
        else {
            if(i>=size)break;
            const uchar flag=data[i++];
            if(flag==0){x=0;++y;continue;}
            run=flag&0x3F;
            if(flag&0x40){if(i>=size)break;run=(run<<8)|data[i++];}
            if(flag&0x80){if(i>=size)break;colour=data[i++];}
        }
        if(x<object.width) {
            const int fill=std::min(run,object.width-x);
            std::memset(pixels+qint64(y)*object.width+x,colour,size_t(fill));
        }
        x=std::min(object.width,x+run);
    }
    object.rle.clear();object.rle.squeeze();
}

struct PgsState {
    QHash<int,PgsObject> objects;
    QHash<int,PgsWindow> windows;
    QHash<int,PgsPalette> palettes;
    qint64 work=0;          // pixels decoded and composed so far
    bool exhausted=false;   // maximumPixelWork reached
    bool spend(qint64 pixels){if(work+pixels>maximumPixelWork){exhausted=true;return false;}work+=pixels;return true;}
    void reset(){objects.clear();windows.clear();palettes.clear();}
    qint64 decodedBytes() const {
        qint64 total=0;for(const auto& o:objects)total+=o.pixels.size();return total;
    }
};

// Builds the picture a display set shows. False when it shows nothing (a "clear").
bool composePgs(const PgsComposition& pcs,PgsState& state,ImageCue& cue,int& skipped) {
    if(pcs.objects.isEmpty())return false;
    const auto palette=state.palettes.constFind(pcs.paletteId);
    if(palette==state.palettes.constEnd()){++skipped;return false;}
    const bool hd=pcs.height>576;
    QVector<QRgb> table(256);
    int transparent=-1;
    for(int i=0;i<256;++i) {
        table[i]=pgsColour((*palette)[size_t(i)],hd);
        if(transparent<0 && qAlpha(table[i])==0)transparent=i;
    }
    if(transparent<0) {   // every entry opaque: fill with the most transparent one
        transparent=0;
        for(int i=1;i<256;++i)if(qAlpha(table[i])<qAlpha(table[transparent]))transparent=i;
    }
    struct Placement { const PgsObject* object; QPoint source; QRect target; };
    QVector<Placement> placements;
    QRect bounds;
    const QRect video(0,0,pcs.width,pcs.height);
    for(const auto& co:pcs.objects) {
        auto found=state.objects.find(co.objectId);
        if(found==state.objects.end() || found->width<=0 || found->height<=0){++skipped;continue;}
        if(!found->decoded) {
            if(state.decodedBytes()+qint64(found->width)*found->height>maximumObjectBytes){++skipped;continue;}
            if(!state.spend(qint64(found->width)*found->height))return false;
            decodePgsObject(*found);
        }
        QRect source(0,0,found->width,found->height);
        if(co.cropped)source&=co.crop;
        if(source.isEmpty())continue;
        const QRect target(QPoint(co.x,co.y),source.size());
        QRect clip=video;
        const auto window=state.windows.constFind(co.windowId);
        if(window!=state.windows.constEnd())clip&=window->rect;
        const QRect visible=target&clip;
        if(visible.isEmpty())continue;
        placements.push_back({&*found,source.topLeft()+(visible.topLeft()-target.topLeft()),visible});
        bounds|=visible;
    }
    if(bounds.isEmpty())return false;
    if(!state.spend(2*qint64(bounds.width())*bounds.height()))return false;
    QImage image(bounds.size(),QImage::Format_Indexed8);
    if(image.isNull()){++skipped;return false;}
    image.setColorTable(table);
    image.fill(uint(transparent));
    for(const auto& p:placements) {
        const int stride=p.object->width;
        const auto* src=reinterpret_cast<const uchar*>(p.object->pixels.constData());
        for(int row=0;row<p.target.height();++row) {
            const uchar* from=src+qint64(p.source.y()+row)*stride+p.source.x();
            uchar* to=image.scanLine(p.target.y()-bounds.y()+row)+(p.target.x()-bounds.x());
            for(int x=0;x<p.target.width();++x)if(qAlpha(table[from[x]]))to[x]=from[x];
        }
    }
    QRect rect=bounds;
    if(!trimToVisible(image,rect))return false;
    cue.image=image;cue.rect=rect;cue.videoSize=video.size();
    cue.forced=std::any_of(pcs.objects.cbegin(),pcs.objects.cend(),[](const PgsCompositionObject& o){return o.forced;});
    return true;
}

// ---------------------------------------------------------------------------------------------
// VobSub
// ---------------------------------------------------------------------------------------------
struct IdxEntry { qint64 ms=0; qint64 filepos=0; };
struct IdxTrack { QString language; int index=-1; qint64 delay=0; QVector<IdxEntry> entries; };
struct IdxFile {
    QSize size{720,480};
    QPoint origin;
    std::array<QRgb,16> palette{};
    bool customColours=false;
    std::array<QRgb,4> custom{};
    std::array<bool,4> customTransparent{};
    qint64 timeOffset=0;
    int langIdx=-1;
    bool recognised=false;
    QVector<IdxTrack> tracks;
};

// "hh:mm:ss:ms", optionally negative (delay lines).
bool parseIdxTime(QString text,qint64& ms) {
    text=text.trimmed();
    bool negative=false;
    if(text.startsWith('-')){negative=true;text=text.mid(1);}
    else if(text.startsWith('+'))text=text.mid(1);
    const auto parts=text.split(':');
    if(parts.size()!=4)return false;
    qint64 v[4];
    for(int i=0;i<4;++i){bool ok=false;v[i]=parts[i].trimmed().toLongLong(&ok);if(!ok || v[i]<0 || v[i]>100000)return false;}
    ms=((v[0]*60+v[1])*60+v[2])*1000+v[3];
    if(negative)ms=-ms;
    return true;
}

IdxFile parseIdx(const QByteArray& bytes) {
    IdxFile idx;
    // ffmpeg's dvdsub default palette, used when the .idx has none.
    const quint32 fallback[16]={0x000000,0x0000ff,0x00ff00,0xff0000,0xffff00,0xff00ff,0x00ffff,0xffffff,
                                0x808000,0x8080ff,0x800080,0x80ff80,0x008080,0xff8080,0x555555,0xaaaaaa};
    for(int i=0;i<16;++i)idx.palette[size_t(i)]=0xff000000u|fallback[i];
    int entries=0;
    for(QString line:QString::fromUtf8(bytes).split('\n')) {
        line=line.trimmed();
        if(line.isEmpty() || line.startsWith('#'))continue;
        const int colon=line.indexOf(':');
        if(colon<=0)continue;
        const QString key=line.left(colon).trimmed().toLower();
        const QString value=line.mid(colon+1).trimmed();
        if(key=="size") {
            const auto wh=value.split('x');
            bool okW=false,okH=false;
            if(wh.size()==2){const int w=wh[0].trimmed().toInt(&okW),h=wh[1].trimmed().toInt(&okH);
                if(okW && okH && w>0 && h>0 && w<=maximumDimension && h<=maximumDimension){idx.size=QSize(w,h);idx.recognised=true;}}
        } else if(key=="org") {
            const auto xy=value.split(',');
            if(xy.size()==2){bool a=false,b=false;const int x=xy[0].trimmed().toInt(&a),y=xy[1].trimmed().toInt(&b);
                if(a && b && std::abs(x)<=maximumDimension && std::abs(y)<=maximumDimension)idx.origin=QPoint(x,y);}
        } else if(key=="palette") {
            const auto colours=value.split(',');
            for(int i=0;i<16 && i<colours.size();++i){bool ok=false;const uint c=colours[i].trimmed().toUInt(&ok,16);
                if(ok)idx.palette[size_t(i)]=0xff000000u|(c&0xffffffu);}
            idx.recognised=true;
        } else if(key=="custom colors") {
            // "ON, tridx: 1000, colors: 000000, ffffff, 808080, 404040"
            idx.customColours=value.startsWith("ON",Qt::CaseInsensitive);
            const int t=value.indexOf("tridx:",0,Qt::CaseInsensitive);
            if(t>=0){const QString bits=value.mid(t+6).trimmed().left(4);
                for(int i=0;i<4 && i<bits.size();++i)idx.customTransparent[size_t(i)]=bits[i]=='1';}
            const int c=value.indexOf("colors:",0,Qt::CaseInsensitive);
            if(c>=0){const auto colours=value.mid(c+7).split(',');
                for(int i=0;i<4 && i<colours.size();++i){bool ok=false;const uint v=colours[i].trimmed().toUInt(&ok,16);
                    if(ok)idx.custom[size_t(i)]=0xff000000u|(v&0xffffffu);}}
        } else if(key=="time offset") {
            bool ok=false;const qint64 v=value.toLongLong(&ok);
            if(ok && std::llabs(v)<=24ll*3600*1000)idx.timeOffset=v;
        } else if(key=="langidx") {
            bool ok=false;const int v=value.toInt(&ok);if(ok)idx.langIdx=v;
        } else if(key=="id") {
            if(idx.tracks.size()>=64)continue;
            IdxTrack track;
            track.language=value.section(',',0,0).trimmed();
            const int at=value.indexOf("index:",0,Qt::CaseInsensitive);
            if(at>=0){bool ok=false;const int v=value.mid(at+6).trimmed().toInt(&ok);if(ok && v>=0 && v<32)track.index=v;}
            if(track.index<0)track.index=int(idx.tracks.size())&0x1F;
            idx.tracks.push_back(track);idx.recognised=true;
        } else if(key=="delay") {
            qint64 d=0;
            if(!idx.tracks.isEmpty() && parseIdxTime(value,d) && std::llabs(d)<=24ll*3600*1000)idx.tracks.last().delay+=d;
        } else if(key=="timestamp") {
            if(entries>=maximumIdxEntries)continue;
            const QString time=value.section(',',0,0);
            const int at=value.indexOf("filepos:",0,Qt::CaseInsensitive);
            qint64 ms=0;bool ok=false;
            if(at<0 || !parseIdxTime(time,ms))continue;
            const qint64 pos=value.mid(at+8).trimmed().toLongLong(&ok,16);
            if(!ok || pos<0)continue;
            if(idx.tracks.isEmpty()){IdxTrack t;t.index=0;idx.tracks.push_back(t);}
            auto& track=idx.tracks.last();
            track.entries.push_back({ms+track.delay+idx.timeOffset,pos});
            ++entries;idx.recognised=true;
        }
    }
    return idx;
}

qint64 readPts(const uchar* p) {
    return (qint64((p[0]>>1)&7)<<30)|(qint64(p[1])<<22)|(qint64(p[2]>>1)<<15)|(qint64(p[3])<<7)|qint64(p[4]>>1);
}

// Walks MPEG program-stream packets from `pos`, handing each private-stream-1 payload (substream id,
// PTS if present, data) to `visit`, which returns false to stop. Pack headers (MPEG-1 and -2),
// system headers, padding and other streams are skipped by their length; lost sync is recovered by
// searching for the next start code. Every step advances, and `maxPackets` (<0: unbounded) caps it.
template<class Visit>
void walkProgramStream(const QByteArray& stream,qsizetype pos,int maxPackets,Visit&& visit) {
    const auto* d=reinterpret_cast<const uchar*>(stream.constData());
    const qsizetype n=stream.size();
    static const QByteArray startCode("\0\0\1",3);
    int packets=0;
    while(pos>=0 && pos+4<=n) {
        if(maxPackets>=0 && packets++>=maxPackets)return;
        if(!(d[pos]==0 && d[pos+1]==0 && d[pos+2]==1)) {
            pos=stream.indexOf(startCode,pos+1);
            continue;
        }
        const int code=d[pos+3];
        if(code==0xBA) {
            if(pos+5>n)return;
            if((d[pos+4]&0xC0)==0x40){if(pos+14>n)return;pos+=14+(d[pos+13]&7);}
            else pos+=12;
            continue;
        }
        if(code==0xB9)return;
        if(code<0xBB){pos+=4;continue;}
        if(pos+6>n)return;
        const qsizetype end=std::min<qsizetype>(n,pos+6+be16(d+pos+4));
        if(code==0xBD) {
            qsizetype q=pos+6;bool hasPts=false;qint64 pts=0;bool ok=true;
            if(q<end && (d[q]&0xC0)==0x80) {           // MPEG-2 PES header
                if(q+3>end)ok=false;
                else {
                    const int flags=d[q+1],length=d[q+2];
                    if(q+3+length>end)ok=false;
                    else{if((flags&0x80) && length>=5){pts=readPts(d+q+3);hasPts=true;}q+=3+length;}
                }
            } else {                                     // MPEG-1 PES header
                int stuffing=0;
                while(q<end && d[q]==0xFF && stuffing++<16)++q;
                if(q<end && (d[q]&0xC0)==0x40)q+=2;
                if(q<end) {
                    if((d[q]&0xF0)==0x20){if(q+5<=end){pts=readPts(d+q);hasPts=true;}q+=5;}
                    else if((d[q]&0xF0)==0x30){if(q+10<=end){pts=readPts(d+q);hasPts=true;}q+=10;}
                    else if(d[q]==0x0F)q+=1;
                }
            }
            if(ok && q<end && !visit(int(d[q]),hasPts,pts,d+q+1,end-q-1))return;
        }
        pos=end;
    }
}

// Reassembles the SPU of substream `id` that starts at or after `pos`.
QByteArray readSpuAt(const QByteArray& sub,qsizetype pos,int id) {
    QByteArray spu;int expected=-1;
    walkProgramStream(sub,pos,maximumSpuPackets,[&](int stream,bool hasPts,qint64,const uchar* data,qsizetype size) {
        if(stream!=id)return true;
        if(!spu.isEmpty() && hasPts)return false;      // the next SPU began: this one is truncated
        spu.append(reinterpret_cast<const char*>(data),size);
        if(expected<0 && spu.size()>=2)expected=be16(reinterpret_cast<const uchar*>(spu.constData()));
        return !(expected>=0 && spu.size()>=expected);
    });
    if(expected<4 || spu.size()<expected)return {};
    spu.truncate(expected);
    return spu;
}

struct SpuPicture {
    QImage image;
    QRect rect;
    qint64 startMs=0,stopMs=-1;
    bool forced=false;
};

struct NibbleReader {
    const uchar* data;qsizetype end;qsizetype nibble;  // position in nibbles
    bool next(int& v){if(nibble>=end*2)return false;const uchar b=data[nibble>>1];v=(nibble&1)?(b&0x0F):(b>>4);++nibble;return true;}
    void align(){nibble=(nibble+1)&~qsizetype(1);}
};

// Decodes one SPU (control sequences + interlaced 2-bit RLE) against the .idx palette.
bool decodeSpu(const QByteArray& spu,const IdxFile& idx,SpuPicture& picture,qint64& work) {
    const auto* d=reinterpret_cast<const uchar*>(spu.constData());
    const qsizetype size=spu.size();
    if(size<4)return false;
    const int controlStart=be16(d+2);
    if(controlStart<4 || controlStart+4>size)return false;
    int colours[4]={0,1,2,3},alpha[4]={0,15,15,15};
    int x1=-1,x2=-1,y1=-1,y2=-1,fieldTop=-1,fieldBottom=-1;
    bool started=false;
    qsizetype at=controlStart;
    for(int sequence=0;sequence<64 && at+4<=size;++sequence) {
        const qint64 ms=qint64(be16(d+at))*1024/90;   // dates are in 1024/90000 s units
        const int next=be16(d+at+2);
        qsizetype p=at+4;
        bool done=false;
        while(p<size && !done) {
            const int command=d[p++];
            switch(command) {
            case 0x00: picture.forced=true; [[fallthrough]];
            case 0x01: if(!started){picture.startMs=ms;started=true;} break;
            case 0x02: if(picture.stopMs<0 && be16(d+at)!=0xFFFF)picture.stopMs=ms; break;   // 0xFFFF: "until replaced"
            case 0x03: if(p+2>size){done=true;break;}
                colours[3]=d[p]>>4;colours[2]=d[p]&15;colours[1]=d[p+1]>>4;colours[0]=d[p+1]&15;p+=2;break;
            case 0x04: if(p+2>size){done=true;break;}
                alpha[3]=d[p]>>4;alpha[2]=d[p]&15;alpha[1]=d[p+1]>>4;alpha[0]=d[p+1]&15;p+=2;break;
            case 0x05: if(p+6>size){done=true;break;}
                x1=(d[p]<<4)|(d[p+1]>>4);x2=((d[p+1]&15)<<8)|d[p+2];
                y1=(d[p+3]<<4)|(d[p+4]>>4);y2=((d[p+4]&15)<<8)|d[p+5];p+=6;break;
            case 0x06: if(p+4>size){done=true;break;}
                fieldTop=be16(d+p);fieldBottom=be16(d+p+2);p+=4;break;
            case 0x07: {   // CHG_COLCON: a self-sized block this decoder does not apply
                if(p+2>size){done=true;break;}
                const int length=be16(d+p);
                if(length<2){done=true;break;}
                p+=length;break;
            }
            default: done=true; break;  // 0xFF ends the sequence; anything else is unknown
            }
        }
        if(next<=at || next+4>size)break;
        at=next;
    }
    if(x1<0 || fieldTop<0 || fieldBottom<0 || x2<x1 || y2<y1)return false;
    if(picture.stopMs>=0 && picture.stopMs<=picture.startMs)picture.stopMs=-1;
    const int width=x2-x1+1,height=y2-y1+1;
    if(width>maximumDimension || height>maximumDimension)return false;
    if(work+2*qint64(width)*height>maximumPixelWork){work=maximumPixelWork+1;return false;}
    work+=2*qint64(width)*height;
    QVector<QRgb> table(4);
    for(int i=0;i<4;++i) {
        const QRgb rgb=idx.customColours?idx.custom[size_t(i)]:idx.palette[size_t(colours[i])];
        const int a=(idx.customColours && idx.customTransparent[size_t(i)])?0:alpha[i]*17;
        table[i]=qRgba(qRed(rgb),qGreen(rgb),qBlue(rgb),a);
    }
    QImage image(width,height,QImage::Format_Indexed8);
    if(image.isNull())return false;
    image.setColorTable(table);
    image.fill(0u);
    for(int field=0;field<2;++field) {
        const int offset=field?fieldBottom:fieldTop;
        if(offset>=size)continue;
        NibbleReader reader{d,size,qsizetype(offset)*2};
        bool ok=true;
        for(int y=field;y<height && ok;y+=2) {
            uchar* line=image.scanLine(y);
            int x=0;
            while(x<width) {
                int v=0,n=0;
                if(!reader.next(v)){ok=false;break;}
                if(v<0x4){if(!reader.next(n)){ok=false;break;}v=(v<<4)|n;
                    if(v<0x10){if(!reader.next(n)){ok=false;break;}v=(v<<4)|n;
                        if(v<0x40){if(!reader.next(n)){ok=false;break;}v=(v<<4)|n;}}}
                int run=v>>2;
                if(run==0)run=width-x;            // run to the end of the line
                const int fill=std::min(run,width-x);
                std::memset(line+x,v&3,size_t(fill));
                x+=fill;
            }
            reader.align();
        }
    }
    QRect rect(QPoint(x1,y1)+idx.origin,QSize(width,height));
    if(!trimToVisible(image,rect))return false;
    picture.image=image;picture.rect=rect;
    return true;
}

QString summary(int skipped,const Timeline& timeline,const QString& what) {
    QStringList parts;
    if(skipped)parts<<QString("%1 %2 skipped").arg(skipped).arg(what);
    if(timeline.dropped)parts<<QString("%1 empty-duration cue(s) dropped").arg(timeline.dropped);
    if(timeline.full)parts<<QString("stopped at the %1-cue / %2 MB limit").arg(maximumCues).arg(maximumImageBytes/(1024*1024));
    return parts.join("; ");
}

void sortCues(QVector<ImageCue>& cues) {
    std::stable_sort(cues.begin(),cues.end(),[](const ImageCue& a,const ImageCue& b){return a.startMs<b.startMs;});
}
} // namespace

bool parsePgs(const QByteArray& sup,QVector<ImageCue>& out,QString* reason) {
    out.clear();
    auto fail=[&](const QString& why){if(reason)*reason=why;out.clear();return false;};
    if(sup.isEmpty())return fail("empty PGS stream");
    if(sup.size()>maximumInputBytes)return fail(QString("PGS stream is larger than %1 MB").arg(maximumInputBytes/(1024*1024)));
    if(!sup.startsWith("PG"))return fail("not a PGS stream (no \"PG\" segment header)");

    const auto* d=reinterpret_cast<const uchar*>(sup.constData());
    const qsizetype n=sup.size();
    Timeline timeline(out);
    PgsState state;
    PgsComposition pending;bool havePending=false;
    ImageCue open;bool isOpen=false;
    int skipped=0,displaySets=0;

    auto finish=[&](const PgsComposition& pcs) {
        ++displaySets;
        ImageCue cue;
        const bool shows=composePgs(pcs,state,cue,skipped);
        if(isOpen) {
            if(shows && cue.rect==open.rect && cue.image==open.image)return;   // same picture again
            open.endMs=pcs.ms;
            timeline.add(std::move(open));
            open=ImageCue();isOpen=false;
        }
        if(shows){cue.startMs=pcs.ms;open=std::move(cue);isOpen=true;}
    };

    qsizetype pos=0;
    while(pos+13<=n && !timeline.full && !state.exhausted) {
        if(d[pos]!='P' || d[pos+1]!='G') {
            ++skipped;
            pos=sup.indexOf("PG",pos+1);
            if(pos<0)break;
            continue;
        }
        const qint64 ms=qint64(be32(d+pos+2))/90;
        const int type=d[pos+10];
        const int length=be16(d+pos+11);
        const qsizetype body=pos+13;
        if(body+length>n){++skipped;break;}   // truncated tail
        const uchar* s=d+body;
        switch(type) {
        case 0x16: {   // PCS
            if(havePending){finish(pending);havePending=false;}
            if(length<11){++skipped;break;}
            PgsComposition pcs;
            pcs.ms=ms;pcs.width=be16(s);pcs.height=be16(s+2);pcs.state=s[7];pcs.paletteId=s[9];
            const int count=s[10];
            qsizetype p=11;
            for(int i=0;i<count && i<maximumPgsCompositionObjects;++i) {
                if(p+8>length)break;
                PgsCompositionObject co;
                co.objectId=be16(s+p);co.windowId=s[p+2];
                const int flags=s[p+3];
                co.cropped=flags&0x80;co.forced=flags&0x40;
                co.x=be16(s+p+4);co.y=be16(s+p+6);p+=8;
                if(co.cropped){if(p+8>length)break;co.crop=QRect(be16(s+p),be16(s+p+2),be16(s+p+4),be16(s+p+6));p+=8;}
                pcs.objects.push_back(co);
            }
            if(pcs.width<=0 || pcs.height<=0 || pcs.width>maximumDimension || pcs.height>maximumDimension){++skipped;break;}
            if(pcs.state&0x80)state.reset();   // epoch start: everything defined before is gone
            pending=pcs;havePending=true;
            break;
        }
        case 0x17: {   // WDS
            if(length<1)break;
            const int count=s[0];
            for(int i=0;i<count;++i) {
                const qsizetype p=1+qsizetype(i)*9;
                if(p+9>length)break;
                if(state.windows.size()>=maximumPgsWindows && !state.windows.contains(s[p]))break;
                state.windows[s[p]].rect=QRect(be16(s+p+1),be16(s+p+3),be16(s+p+5),be16(s+p+7));
            }
            break;
        }
        case 0x14: {   // PDS: entries update the palette in place
            if(length<2)break;
            auto& palette=state.palettes[s[0]];
            for(qsizetype p=2;p+5<=length;p+=5) {
                auto& e=palette[s[p]];
                e.y=s[p+1];e.cr=s[p+2];e.cb=s[p+3];e.a=s[p+4];
            }
            break;
        }
        case 0x15: {   // ODS, possibly fragmented over several segments
            if(length<4){++skipped;break;}
            const int id=be16(s);
            const int sequence=s[3];
            if(sequence&0x80) {
                if(length<11){++skipped;state.objects.remove(id);break;}
                const int width=be16(s+7),height=be16(s+9);
                if(width<=0 || height<=0 || width>maximumDimension || height>maximumDimension){++skipped;state.objects.remove(id);break;}
                if(!state.objects.contains(id) && state.objects.size()>=maximumPgsObjects){++skipped;break;}
                PgsObject object;object.width=width;object.height=height;
                object.rle=QByteArray(reinterpret_cast<const char*>(s+11),length-11);
                state.objects[id]=std::move(object);
            } else {
                auto found=state.objects.find(id);
                if(found==state.objects.end() || found->decoded){++skipped;break;}
                found->rle.append(reinterpret_cast<const char*>(s+4),length-4);
            }
            if(sequence&0x40) {
                auto found=state.objects.find(id);
                if(found!=state.objects.end() && !found->decoded) {
                    if(state.decodedBytes()+qint64(found->width)*found->height>maximumObjectBytes){++skipped;state.objects.erase(found);}
                    else if(state.spend(qint64(found->width)*found->height))decodePgsObject(*found);
                }
            }
            break;
        }
        case 0x80:     // END of display set
            if(havePending){finish(pending);havePending=false;}
            break;
        default: break;
        }
        pos=body+length;
    }
    if(havePending && !timeline.full && !state.exhausted)finish(pending);
    if(isOpen && !timeline.full){open.endMs=open.startMs+defaultDurationMs;timeline.add(std::move(open));}
    sortCues(out);
    if(out.isEmpty())return fail(displaySets?QString("no displayable picture in %1 PGS display set(s)").arg(displaySets)
                                            :QString("no complete PGS display set"));
    if(reason){*reason=summary(skipped,timeline,"segment(s)/display set(s)");
        if(state.exhausted)*reason+=QString(reason->isEmpty()?"":"; ")+"stopped at the pixel-work limit";}
    return true;
}

bool parseVobSub(const QByteArray& idxBytes,const QByteArray& sub,int track,QVector<ImageCue>& out,QString* reason) {
    out.clear();
    auto fail=[&](const QString& why){if(reason)*reason=why;out.clear();return false;};
    if(idxBytes.size()>maximumInputBytes || sub.size()>maximumInputBytes)
        return fail(QString("VobSub input is larger than %1 MB").arg(maximumInputBytes/(1024*1024)));
    if(sub.isEmpty())return fail("empty VobSub .sub");
    const IdxFile idx=parseIdx(idxBytes);
    if(!idx.recognised)return fail("not a VobSub .idx (no size, palette, id or timestamp line)");

    // Pick the language: by "index:", then by position; negative = the file's langidx, else the first.
    const IdxTrack* chosen=nullptr;
    const int wanted=track>=0?track:std::max(0,idx.langIdx);
    for(const auto& t:idx.tracks)if(t.index==wanted){chosen=&t;break;}
    if(!chosen && wanted<idx.tracks.size())chosen=&idx.tracks[wanted];
    if(!chosen && track<0 && !idx.tracks.isEmpty())chosen=&idx.tracks.first();
    if(!chosen && !idx.tracks.isEmpty())return fail(QString("VobSub has no track %1 (%2 in the .idx)").arg(track).arg(idx.tracks.size()));
    const int streamId=0x20+((chosen?chosen->index:std::max(0,wanted))&0x1F);

    struct Raw { qint64 ms; QByteArray spu; };
    QVector<Raw> raws;
    int skipped=0;
    if(chosen && !chosen->entries.isEmpty()) {
        for(const auto& e:chosen->entries) {
            if(e.filepos>=sub.size()){++skipped;continue;}
            QByteArray spu=readSpuAt(sub,qsizetype(e.filepos),streamId);
            if(spu.isEmpty()){++skipped;continue;}
            raws.push_back({e.ms,spu});
        }
    } else {
        // No timestamps for this language: take every SPU of the substream with its packet PTS.
        QByteArray spu;qint64 pts=0;int expected=-1;
        const qint64 offset=idx.timeOffset+(chosen?chosen->delay:0);
        walkProgramStream(sub,0,-1,[&](int stream,bool hasPts,qint64 packetPts,const uchar* data,qsizetype size) {
            if(stream!=streamId)return true;
            if(hasPts){if(!spu.isEmpty())++skipped;spu.clear();expected=-1;pts=packetPts;}
            else if(spu.isEmpty())return true;          // continuation without a start
            spu.append(reinterpret_cast<const char*>(data),size);
            if(expected<0 && spu.size()>=2)expected=be16(reinterpret_cast<const uchar*>(spu.constData()));
            if(expected>=0 && spu.size()>=expected) {
                if(expected>=4){spu.truncate(expected);raws.push_back({pts/90+offset,spu});}
                else ++skipped;
                spu.clear();expected=-1;
            }
            return raws.size()<maximumCues;
        });
    }

    QVector<ImageCue> cues;
    QVector<bool> openEnded;
    qint64 bytes=0,work=0;bool full=false;
    for(const auto& raw:raws) {
        SpuPicture picture;
        if(!decodeSpu(raw.spu,idx,picture,work)){if(work>maximumPixelWork)break;++skipped;continue;}
        if(cues.size()>=maximumCues || bytes+picture.image.sizeInBytes()>maximumImageBytes){full=true;break;}
        bytes+=picture.image.sizeInBytes();
        ImageCue cue;
        cue.startMs=raw.ms+picture.startMs;
        cue.endMs=picture.stopMs>=0?raw.ms+picture.stopMs:-1;
        cue.image=picture.image;cue.rect=picture.rect;cue.videoSize=idx.size;cue.forced=picture.forced;
        openEnded.push_back(cue.endMs<0);
        cues.push_back(std::move(cue));
    }
    // A DVD has one subpicture plane: a new SPU replaces the one on screen, so every cue ends at the
    // next one's start at the latest. A cue with no stop command (or the 0xFFFF "until replaced"
    // date) runs to the next one when that is close, otherwise for the default time.
    QVector<int> order(cues.size());
    for(int i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return cues[a].startMs<cues[b].startMs;});
    for(int k=0;k<order.size();++k) {
        auto& cue=cues[order[k]];
        const qint64 next=k+1<order.size()?cues[order[k+1]].startMs:-1;
        if(openEnded[order[k]]) {
            cue.endMs=cue.startMs+defaultDurationMs;
            if(next>cue.startMs && next-cue.startMs<=maximumGapAsDurationMs)cue.endMs=next;
        } else if(next>cue.startMs && cue.endMs>next) cue.endMs=next;
    }
    Timeline timeline(out);
    for(int i:order)timeline.add(std::move(cues[i]));
    timeline.full=timeline.full || full;
    sortCues(out);
    if(out.isEmpty())return fail(raws.isEmpty()?QString("no subpicture for stream 0x%1 in the .sub").arg(streamId,2,16,QChar('0'))
                                               :QString("none of %1 subpicture(s) decoded").arg(raws.size()));
    if(reason) {
        *reason=summary(skipped,timeline,"subpicture(s)");
        if(work>maximumPixelWork)*reason+=QString(reason->isEmpty()?"":"; ")+"stopped at the pixel-work limit";
    }
    return true;
}

const ImageCue* cueAt(const QVector<ImageCue>& cues,qint64 ms) {
    auto it=std::upper_bound(cues.cbegin(),cues.cend(),ms,[](qint64 v,const ImageCue& c){return v<c.startMs;});
    // Walk back over the cues that have started; the latest one still running wins. Overlaps in real
    // streams are shallow, so a short bounded walk is enough.
    for(int steps=0;it!=cues.cbegin() && steps<64;++steps) {
        --it;
        if(ms<it->endMs)return &*it;
    }
    return nullptr;
}
}
