#include "edge_decode.h"
#include <QtEndian>
#include <bit>
#include <cmath>

namespace xmb {
// Index stream layout and triangle reconstruction cross-checked against SoulsFormatsNEXT's EdgeGeom.cs:
// https://github.com/soulsmods/SoulsFormatsNEXT/blob/master/SoulsFormats/Utilities/EdgeGeom.cs
// BE header: delta count, delta bias, 1-bit byte count, delta bit width, padding. MSB-first streams follow:
// sequential/explicit selectors, two-bit triangle reuse codes, then fixed-width deltas. Deltas subtract the
// bias and add the value eight entries earlier (16-bit arithmetic). Selector zero emits the next sequential
// vertex, one consumes a delta. Triangle codes 0/1/2 reuse an edge, 3 starts three new vertices.
bool decodeEdge(const QByteArray& b,QVector<EdgeVertex>& vertices,QVector<quint32>& indices,QString* error) {
    vertices.clear();indices.clear();if(error)error->clear();
    const auto fail=[&](const QString& message){vertices.clear();indices.clear();if(error)*error=message;return false;};
    const auto valid=[&](quint64 o,quint64 n){return o<=quint64(b.size()) && n<=quint64(b.size())-o;};
    const auto u16=[&](quint64 o){return qFromBigEndian<quint16>(b.constData()+o);};
    const auto u32=[&](quint64 o){return qFromBigEndian<quint32>(b.constData()+o);};
    const auto byte=[&](quint64 o){return quint8(b.at(qsizetype(o)));};
    if(!valid(0,8))return fail(QStringLiteral("truncated Edge header"));
    if(u32(0)!=1)return fail(QStringLiteral("unsupported Edge segment count (only one segment supported)"));
    const quint32 seg=u32(4);
    if(!valid(seg,0x78))return fail(QStringLiteral("truncated segment header"));
    const quint32 count=u16(seg+8), indexCount=u16(seg+10);
    const quint32 index=u32(seg+0x10), indexSize=u16(seg+0x14);
    const quint32 pos=u32(seg+0x18),normal=u32(seg+0x1c),uv=u32(seg+0x2c);
    const quint32 desc=u32(seg+0x70),uvDesc=u32(seg+0x74);
    // Only the descriptor combination verified in plane.edge is currently understood. Reject rather than
    // silently interpret other Edge vertex encodings as these formats.
    const QByteArray d1=QByteArray::fromHex("0210020000000000000203010c0000000c06010204020000");
    const QByteArray d2=QByteArray::fromHex("01040100000000000003020504080000");
    if(!valid(desc,d1.size()) || !valid(uvDesc,d2.size()) || b.mid(desc,d1.size())!=d1 || b.mid(uvDesc,d2.size())!=d2)
        return fail(QStringLiteral("unsupported vertex stream format"));
    if(!count || !indexCount || indexCount%3 || !valid(pos,quint64(count)*12) || !valid(normal,quint64(count)*4) || !valid(uv,quint64(count)*4))
        return fail(QStringLiteral("invalid or truncated vertex streams"));
    if(indexSize<8 || !valid(index,indexSize))return fail(QStringLiteral("invalid index stream bounds"));
    const quint32 deltaCount=u16(index),bias=u16(index+2),oneBytes=u16(index+4),bits=byte(index+6),triangles=indexCount/3;
    const quint32 twoBytes=(triangles*2+7)/8,deltaBytes=(deltaCount*bits+7)/8;
    if(bits>16 || byte(index+7)!=0 || quint64(8)+oneBytes+twoBytes+deltaBytes>indexSize)
        return fail(QStringLiteral("invalid compressed index header"));
    const quint64 one=index+8,two=one+oneBytes,nbit=two+twoBytes;
    QVector<quint16> delta;delta.reserve(deltaCount);
    for(quint32 i=0;i<deltaCount;++i) {
        quint32 value=0;
        for(quint32 j=0;j<bits;++j){const quint32 bit=i*bits+j;value=(value<<1)|((byte(nbit+bit/8)>>(7-bit%8))&1);}
        quint16 v=quint16(value-bias);if(i>=8)v=quint16(v+delta[int(i-8)]);delta.push_back(v);
    }
    quint32 sequential=0,explicitIndex=0,selector=0;quint32 tri[3]={0,0,0};
    const auto next=[&](quint32& out)->bool {
        if(selector>=oneBytes*8)return false;
        const bool explicitValue=(byte(one+selector/8)&(0x80u>>(selector%8)))!=0;++selector;
        if(explicitValue){if(explicitIndex>=deltaCount)return false;out=delta[int(explicitIndex++)];}
        else out=sequential++;
        return out<count;
    };
    for(quint32 i=0;i<triangles;++i) {
        const quint32 code=(byte(two+i/4)>>(6-2*(i%4)))&3;
        if(i==0 && code!=3)return fail(QStringLiteral("first triangle reuses an unavailable edge"));
        if(code==0)tri[1]=tri[2];
        else if(code==1)tri[0]=tri[2];
        else if(code==2)std::swap(tri[0],tri[1]);
        if(code==3 && (!next(tri[0]) || !next(tri[1])))return fail(QStringLiteral("invalid index selector stream"));
        if(!next(tri[2]))return fail(QStringLiteral("invalid index selector stream"));
        indices.append({tri[0],tri[1],tri[2]});
    }
    const auto half=[](quint16 h){const int e=(h>>10)&31,m=h&1023;const float s=h>>15?-1.f:1.f;return e==31?(m?NAN:s*INFINITY):(e?s*std::ldexp(1.f+m/1024.f,e-15):s*std::ldexp(float(m),-24));};
    const auto snorm=[](quint32 v,int bits){const int sign=1<<(bits-1);const int n=int(v&((1u<<bits)-1));return float((n^sign)-sign)/float(sign-1);};
    vertices.reserve(count);
    for(quint32 i=0;i<count;++i) {
        const quint32 n=u32(normal+i*4);
        EdgeVertex v;v.position={std::bit_cast<float>(u32(pos+i*12)),std::bit_cast<float>(u32(pos+i*12+4)),std::bit_cast<float>(u32(pos+i*12+8))};
        v.normal={snorm(n,11),snorm(n>>11,11),snorm(n>>22,10)};v.uv={half(u16(uv+i*4)),half(u16(uv+i*4+2))};vertices.push_back(v);
    }
    return true;
}
}
