#include "qrc.h"
#include <QFile>
#include <QtEndian>
#include <stdexcept>

namespace xmb {
namespace {
void range(const QByteArray& bytes,quint64 offset,quint64 size) {
    if(offset>quint64(bytes.size()) || size>quint64(bytes.size())-offset)
        throw std::runtime_error("QRC range exceeds container");
}
quint32 word(const QByteArray& bytes,quint64 offset) {
    range(bytes,offset,4);return qFromBigEndian<quint32>(bytes.constData()+offset);
}
QByteArray unpack(const QByteArray& bytes,quint64 offset,quint32 size,quint32 expanded) {
    if(!expanded || expanded>32*1024*1024)throw std::runtime_error("QRC expansion exceeds 32 MiB");
    range(bytes,offset,size);
    QByteArray framed(4,Qt::Uninitialized);qToBigEndian(expanded,framed.data());
    framed.append(bytes.constData()+offset,size);
    const auto result=qUncompress(framed);
    if(result.size()!=expanded)throw std::runtime_error("QRC expanded length mismatch");
    return result;
}
QString string(const QByteArray& bytes,quint32 base,quint32 size,quint64 offset) {
    if(offset>=size)throw std::runtime_error("QRC string offset exceeds table");
    const auto end=bytes.indexOf('\0',quint64(base)+offset);
    if(end<0 || quint64(end)>=quint64(base)+size)throw std::runtime_error("QRC string is unterminated");
    return QString::fromUtf8(bytes.constData()+base+offset,end-base-offset);
}
}
QrcArchive::QrcArchive(const QString& path) {
    QFile source(path);
    if(!source.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open local QRC");
    if(source.size()>64*1024*1024)throw std::runtime_error("QRC exceeds 64 MiB");
    bytes_=source.readAll();
    if(bytes_.startsWith("QRCC"))bytes_=unpack(bytes_,8,quint32(bytes_.size()-8),word(bytes_,4));
    if(!bytes_.startsWith("QRCF") || word(bytes_,4)!=0x110)throw std::runtime_error("Unsupported QRC version");
    const auto toc=word(bytes_,8),tocSize=word(bytes_,12),ids=word(bytes_,16),idsSize=word(bytes_,20);
    const auto strings=word(bytes_,24),stringsSize=word(bytes_,28),dataSize=word(bytes_,52);
    data_=word(bytes_,48);
    range(bytes_,toc,tocSize);range(bytes_,ids,idsSize);range(bytes_,strings,stringsSize);range(bytes_,data_,dataSize);
    for(quint64 offset=0;offset<tocSize;) {
        if(tocSize-offset<28)throw std::runtime_error("Truncated QRC node");
        const quint64 at=quint64(toc)+offset;
        const auto count=word(bytes_,at+4);
        const quint64 length=28+quint64(count)*16;
        if(count>64 || length>tocSize-offset)throw std::runtime_error("Invalid QRC attributes");
        QString id;Entry entry;bool hasSource=false,hasSize=false;
        for(quint32 i=0;i<count;++i) {
            const quint64 attr=at+28+quint64(i)*16;
            const auto name=string(bytes_,strings,stringsSize,word(bytes_,attr));
            const auto type=word(bytes_,attr+4),a=word(bytes_,attr+8),b=word(bytes_,attr+12);
            if(name=="id" && type==7) {
                // ID-table entries begin with a node back-reference, then UTF-8.
                if(quint64(a)+4>idsSize || word(bytes_,quint64(ids)+a)!=offset)throw std::runtime_error("Invalid QRC ID back-reference");
                id=string(bytes_,ids,idsSize,quint64(a)+4);
            } else if(name=="src" && type==6) {
                if(a>dataSize || b>dataSize-a)throw std::runtime_error("QRC file exceeds data table");
                entry.offset=a;entry.size=b;hasSource=true;
            } else if(name=="size" && type==1) {entry.expanded=a;hasSize=true;}
        }
        if(hasSource) {
            if(id.isEmpty() || entries_.contains(id))throw std::runtime_error("Invalid or duplicate QRC file ID");
            if(hasSize && !entry.expanded)throw std::runtime_error("Invalid QRC expanded size");
            entries_.insert(id,entry);
        }
        offset+=length;
    }
}
QByteArray QrcArchive::file(const QString& id) const {
    const auto entry=entries_.constFind(id);
    if(entry==entries_.cend())throw std::runtime_error("Named file is absent from QRC");
    const quint64 start=quint64(data_)+entry->offset;
    return entry->expanded?unpack(bytes_,start,entry->size,entry->expanded):bytes_.mid(start,entry->size);
}
}
