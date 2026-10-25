#include "fragment_capture.h"
#include <QCryptographicHash>
#include <QSet>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace xmb {
namespace {
quint32 word(const QByteArray& bytes,quint64 offset) {
    if(offset>quint64(bytes.size()) || quint64(bytes.size())-offset<4)
        throw std::runtime_error("Cg metadata exceeds the file");
    return qFromBigEndian<quint32>(bytes.constData()+offset);
}
quint32 fpWord(const QByteArray& bytes,int offset) {
    const auto v=word(bytes,offset);return (v<<16)|(v>>16);
}
bool same(const QVector4D& a,const QVector4D& b) {
    for(int k=0;k<4;++k)if(a[k]!=b[k])return false;
    return true;
}
}
bool FragmentCapture::read(const QByteArray& original,const QByteArray& captured,FragmentCapture& result,QString* error) {
    result={};
    const auto fail=[&](const QString& why){if(error)*error=why;return false;};
    try {
        if(original.size()>64*1024*1024 || captured.size()>1024*1024)return fail("Shader capture exceeds size limit");
        if(word(original,0)!=7004)return fail("Expected a fragment Cg program");
        const quint32 count=word(original,12),parameters=word(original,16),length=word(original,24),codeOffset=word(original,28);
        if(!length || length%16 || length!=quint32(captured.size()))return fail("Microcode length does not match");
        if(quint64(codeOffset)+length>quint64(original.size()) || count>65536 || quint64(parameters)+quint64(count)*48>quint64(original.size()))
            return fail("Invalid Cg program ranges");
        const QByteArray code=original.mid(codeOffset,length);
        QSet<int> literalSlots;
        for(int pc=0;pc+16<=code.size();) {
            const auto dst=fpWord(code,pc),a=fpWord(code,pc+4),b=fpWord(code,pc+8),c=fpWord(code,pc+12);
            const auto opcode=((dst>>24)&0x3f)|((b>>31)<<6);
            const bool literal=opcode<0x40 && ((a&3)==2 || (b&3)==2 || (c&3)==2);
            if(literal) {
                if(pc+32>code.size())return fail("Truncated fragment literal");
                literalSlots.insert(pc+16);
            }
            pc+=literal?32:16;
            if(dst&1)break;
        }
        QSet<int> mutableSlots;
        for(quint32 i=0;i<count;++i) {
            const auto p=quint64(parameters)+quint64(i)*48;
            if(!word(original,p+40))continue;
            const auto table=word(original,p+24);if(!table)continue;
            const auto n=word(original,table);
            if(n>65536 || quint64(table)+4+quint64(n)*4>quint64(original.size()))return fail("Invalid embedded constant table");
            for(quint32 j=0;j<n;++j) {
                const auto offset=word(original,quint64(table)+4+quint64(j)*4);
                if(offset>quint32(code.size()) || !literalSlots.contains(int(offset)))return fail("Parameter relocation is not a literal slot");
                mutableSlots.insert(int(offset));
            }
        }
        for(int at=0;at<code.size();++at) {
            if(mutableSlots.contains(at&~15))continue;
            if(code[at]!=captured[at])return fail(QString("Program identity differs at microcode byte %1").arg(at));
        }
        auto patched=original;patched.replace(codeOffset,length,captured);
        QString why;auto program=CgProgram::load(patched,&why);
        if(!program.valid() || program.vertex)return fail("Captured program cannot be decoded: "+why);
        QHash<QString,QVector4D> values;
        for(auto it=program.parameters.cbegin();it!=program.parameters.cend();++it) {
            if(it->constantSlots.isEmpty())continue;
            const int first=it->constantSlots.first();
            if(first<0 || first>=program.constants.size())return fail("Captured uniform slot is out of range");
            const auto value=program.constants[first];
            for(int k=0;k<4;++k)if(!std::isfinite(value[k]))return fail("Captured uniform contains a non-finite value");
            for(const int slot:it->constantSlots) {
                if(slot<0 || slot>=program.constants.size() || !same(value,program.constants[slot]))
                    return fail("Captured uniform copies disagree: "+it.key());
            }
            values.insert(it.key(),value);
        }
        FragmentCapture parsed;parsed.program=std::move(program);parsed.values=std::move(values);
        parsed.originalSha256=QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex();
        parsed.captureSha256=QCryptographicHash::hash(captured,QCryptographicHash::Sha256).toHex();
        result=std::move(parsed);if(error)error->clear();return true;
    }catch(const std::exception& e){return fail(QString::fromUtf8(e.what()));}
}
}
