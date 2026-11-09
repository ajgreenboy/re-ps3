#include "gtf_decode.h"
#include <QtEndian>
#include <array>
#include <algorithm>

namespace xmb {
namespace {
quint64 morton(int x,int y,int width,int height) {
    quint64 index=0,bit=1;
    for(int mask=1;mask<width || mask<height;mask<<=1) {
        if(mask<width){if(x&mask)index|=bit;bit<<=1;}
        if(mask<height){if(y&mask)index|=bit;bit<<=1;}
    }
    return index;
}
QRgb colour565(quint16 value) {
    const int r=value>>11,g=(value>>5)&63,b=value&31;
    return qRgb((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2));
}
}
QImage decodeGtf(const QByteArray& gtf,QString* error) {
    if(error)error->clear();
    const auto fail=[&](const QString& message){if(error)*error=message;return QImage{};};
    if(gtf.size()<0x30)return fail(QStringLiteral("truncated GTF header"));
    const auto* data=reinterpret_cast<const uchar*>(gtf.constData());
    if(!qFromBigEndian<quint32>(data+8))return fail(QStringLiteral("GTF has no textures"));
    const quint64 offset=qFromBigEndian<quint32>(data+0x10);
    const quint64 declared=qFromBigEndian<quint32>(data+0x14);
    const int code=data[0x18],base=code&~0x60;
    const bool linear=code&0x20;
    const int width=qFromBigEndian<quint16>(data+0x20),height=qFromBigEndian<quint16>(data+0x22);
    const quint64 pitch=qFromBigEndian<quint32>(data+0x28);
    if(!width || !height)return fail(QStringLiteral("invalid GTF dimensions"));
    const bool b8=base==0x81,argb=base==0x82 || base==0x85,dxt=base>=0x86 && base<=0x88;
    if(!b8 && !argb && !dxt)return fail(QStringLiteral("unsupported GTF format 0x%1").arg(code,2,16,QChar('0')));
    const quint64 end=quint64(gtf.size());
    if(offset>end || declared>end-offset)return fail(QStringLiteral("GTF texture exceeds file"));
    const quint64 available=declared;
    quint64 stride=0,required=0;
    if(dxt) {
        const quint64 row=quint64((width+3)/4)*(base==0x86?8:16);
        stride=linear && pitch?pitch:row;
        if(stride<row)return fail(QStringLiteral("invalid DXT pitch"));
        required=stride*quint64((height+3)/4);
    } else if(linear) {
        const quint64 row=quint64(width)*(b8?1:4);
        stride=pitch?pitch:row;
        if(stride<row)return fail(QStringLiteral("invalid GTF pitch"));
        required=stride*quint64(height);
    } else {
        if((width&(width-1)) || (height&(height-1)))return fail(QStringLiteral("swizzled GTF dimensions must be powers of two"));
        required=quint64(width)*height*(b8?1:4);
    }
    if(required>available)return fail(QStringLiteral("truncated GTF pixels"));
    QImage image(width,height,QImage::Format_ARGB32);
    if(image.isNull())return fail(QStringLiteral("cannot allocate GTF image"));
    const uchar* pixels=data+offset;
    if(!dxt) {
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const quint64 pos=linear?quint64(y)*stride+quint64(x)*(b8?1:4):morton(x,y,width,height)*(b8?1:4);
            const auto* p=pixels+pos;
            reinterpret_cast<QRgb*>(image.scanLine(y))[x]=b8?qRgba(p[0],p[0],p[0],p[0]):qRgba(p[1],p[2],p[3],p[0]);
        }
        return image;
    }
    const int blockBytes=base==0x86?8:16;
    for(int by=0;by<height;by+=4)for(int bx=0;bx<width;bx+=4) {
        const uchar* block=pixels+quint64(by/4)*stride+quint64(bx/4)*blockBytes;
        const uchar* colours=block+(base==0x86?0:8);
        const quint16 c0=qFromLittleEndian<quint16>(colours),c1=qFromLittleEndian<quint16>(colours+2);
        std::array<QRgb,4> palette{colour565(c0),colour565(c1),0,0};
        if(base!=0x86 || c0>c1)for(int i=0;i<2;++i)
            palette[2+i]=qRgb(((2-i)*qRed(palette[0])+(1+i)*qRed(palette[1]))/3,((2-i)*qGreen(palette[0])+(1+i)*qGreen(palette[1]))/3,((2-i)*qBlue(palette[0])+(1+i)*qBlue(palette[1]))/3);
        else {palette[2]=qRgb((qRed(palette[0])+qRed(palette[1]))/2,(qGreen(palette[0])+qGreen(palette[1]))/2,(qBlue(palette[0])+qBlue(palette[1]))/2);palette[3]=qRgba(0,0,0,0);}
        const quint32 indices=qFromLittleEndian<quint32>(colours+4);
        std::array<int,8> alphas{};quint64 alphaIndices=0;
        if(base==0x88) {
            alphas[0]=block[0];alphas[1]=block[1];
            if(alphas[0]>alphas[1])for(int i=1;i<=6;++i)alphas[i+1]=((7-i)*alphas[0]+i*alphas[1])/7;
            else {for(int i=1;i<=4;++i)alphas[i+1]=((5-i)*alphas[0]+i*alphas[1])/5;alphas[6]=0;alphas[7]=255;}
            for(int i=0;i<6;++i)alphaIndices|=quint64(block[2+i])<<(i*8);
        }
        for(int y=0;y<4 && by+y<height;++y)for(int x=0;x<4 && bx+x<width;++x) {
            const int n=y*4+x;const QRgb c=palette[(indices>>(n*2))&3];
            const int a=base==0x88?alphas[(alphaIndices>>(n*3))&7]:base==0x87?((block[n/2]>>((n&1)*4))&15)*17:qAlpha(c);
            reinterpret_cast<QRgb*>(image.scanLine(by+y))[bx+x]=qRgba(qRed(c),qGreen(c),qBlue(c),a);
        }
    }
    return image;
}
}
