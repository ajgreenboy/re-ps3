#include "firmware_extract.h"
#include "firmware_crypto.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <QtZlib/zlib.h>
#include <cstdio>
#include <functional>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace xmb {
namespace {
#if !defined(XMB_FIRMWARE_NO_EMBEDDED_KEYS) && __has_include("firmware_keys.inc")
#include "firmware_keys.inc"
#else
#include "firmware_keys.example.inc"
#endif
struct Keys { QByteArray pup, erk, riv; };
bool fail(QString* error, const QString& text) { if(error)*error=text; return false; }
quint64 be64(const QByteArray& b, qsizetype at) { return qFromBigEndian<quint64>(b.constData()+at); }
quint32 be32(const QByteArray& b, qsizetype at) { return qFromBigEndian<quint32>(b.constData()+at); }
bool range(quint64 at, quint64 size, quint64 total) { return at<=total && size<=total-at; }
bool keys(Keys& k, QString* error) {
    const QString path=qEnvironmentVariable("XMB_FIRMWARE_KEYS");
    if(path.isEmpty()) {
        k={QByteArray(reinterpret_cast<const char*>(PUP_KEY),64),QByteArray(reinterpret_cast<const char*>(SCEPKG_ERK),32),QByteArray(reinterpret_cast<const char*>(SCEPKG_RIV),16)};
    } else {
        QFile f(path);
        if(!f.open(QIODevice::ReadOnly))return fail(error,QStringLiteral("Cannot read XMB_FIRMWARE_KEYS: %1").arg(f.errorString()));
        if(f.size()>65536)return fail(error,QStringLiteral("Key file is too large"));
        QString text=QString::fromUtf8(f.readAll());
        text.remove(QRegularExpression(QStringLiteral("//[^\n]*|/\\*[\\s\\S]*?\\*/")));
        const auto parse=[&](const QString& name,int length,QByteArray& out) {
            const auto m=QRegularExpression(name+QStringLiteral("\\s*\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}")).match(text);
            if(!m.hasMatch())return false;
            const auto tokens=m.captured(1).split(',',Qt::SkipEmptyParts);
            if(tokens.size()!=length)return false;
            for(const auto& token:tokens) { bool ok=false;const uint v=token.trimmed().toUInt(&ok,0);if(!ok||v>255)return false;out.append(char(v)); }
            return true;
        };
        if(!parse(QStringLiteral("PUP_KEY"),64,k.pup)||!parse(QStringLiteral("SCEPKG_ERK"),32,k.erk)||!parse(QStringLiteral("SCEPKG_RIV"),16,k.riv))
            return fail(error,QStringLiteral("Invalid key declarations in XMB_FIRMWARE_KEYS"));
    }
    if(k.pup==QByteArray(64,0)||k.erk==QByteArray(32,0)||k.riv==QByteArray(16,0))
        return fail(error,QStringLiteral("decryption keys not provided (create native-xmb/src/firmware_keys.inc)"));
    return true;
}
QString field(const QByteArray& b, int at, int length) {
    QByteArray s=b.mid(at,length); const auto end=s.indexOf('\0'); if(end>=0)s.truncate(end);return QString::fromUtf8(s);
}
bool safeName(const QString& name) {
    if(name.isEmpty()||name.startsWith('/')||name.contains('\\')||name.contains(':')||name.contains(QChar::Null))return false;
    for(const auto& part:name.split('/')) {
        if(part==QStringLiteral("..")||part.endsWith(' ')||part.endsWith('.')) {
            if(part!=QStringLiteral("."))return false;
        }
        // Windows device aliases can redirect writes even without a drive prefix.
        const QString base=part.section('.',0,0).toUpper();
        if(base==QStringLiteral("CON")||base==QStringLiteral("PRN")||base==QStringLiteral("AUX")||base==QStringLiteral("NUL")||
           QRegularExpression(QStringLiteral("^(COM|LPT)[0-9]$")).match(base).hasMatch())return false;
        if(part.contains(QRegularExpression(QStringLiteral("[<>\"|?*\\x00-\\x1f]"))))return false;
    }
    return true;
}
bool noLinks(const QString& path) {
    QString p=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for(;;) {
        const QFileInfo info(p);
        if(info.isSymLink())return false;
#ifdef Q_OS_WIN
        const DWORD attr=GetFileAttributesW(reinterpret_cast<const wchar_t*>(p.utf16()));
        if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#endif
        const QString parent=QFileInfo(p).absolutePath();if(parent==p)break;p=parent;
    }
    return true;
}
using TarVisitor=std::function<bool(const QString&,char,const QByteArray&)>;
bool tar(const QByteArray& data,const TarVisitor& visit,QString* error) {
    qsizetype at=0;
    while(range(quint64(at),512,quint64(data.size()))) {
        const QByteArray h=data.mid(at,512);at+=512;
        if(h==QByteArray(512,0)) {
            if(!range(quint64(at),512,quint64(data.size()))||data.mid(at,512)!=QByteArray(512,0))return fail(error,QStringLiteral("Truncated TAR terminator"));
            return true;
        }
        if(h.mid(257,5)!="ustar")return fail(error,QStringLiteral("TAR is not ustar"));
        const auto number=[&](int pos,int len,bool& ok) { return h.mid(pos,len).replace('\0',' ').trimmed().toULongLong(&ok,8); };
        bool ok=false;const quint64 stored=number(148,8,ok);quint64 sum=0;
        for(int i=0;i<512;++i)sum+=(i>=148&&i<156)?32:static_cast<unsigned char>(h[i]);
        if(!ok||stored!=sum)return fail(error,QStringLiteral("TAR checksum mismatch"));
        const quint64 size=number(124,12,ok);
        if(!ok||!range(quint64(at),size,quint64(data.size())))return fail(error,QStringLiteral("Invalid TAR entry size"));
        QString name=field(h,0,100),prefix=field(h,345,155);if(!prefix.isEmpty())name=prefix+'/'+name;
        if(!safeName(name))return fail(error,QStringLiteral("Unsafe TAR path: %1").arg(name));
        const char type=h[156];
        if(type!='0'&&type!='\0'&&type!='5')return fail(error,QStringLiteral("Unsupported TAR entry type for %1 (links/devices forbidden)").arg(name));
        if(!visit(name,type,data.mid(at,qsizetype(size))))return false;
        const quint64 padded=(size+511)/512*512;
        if(!range(quint64(at),padded,quint64(data.size())))return fail(error,QStringLiteral("Truncated TAR padding"));
        at+=qsizetype(padded);
    }
    return fail(error,QStringLiteral("Missing TAR terminator"));
}
bool inflateSection(QByteArray& data,QString* error) {
    z_stream z{};z.next_in=reinterpret_cast<Bytef*>(data.data());z.avail_in=uInt(data.size());
    if(inflateInit2(&z,15+32)!=Z_OK)return fail(error,QStringLiteral("Cannot initialize SCE decompression"));
    QByteArray result;int status=Z_OK;char block[65536];
    while(status==Z_OK) {
        z.next_out=reinterpret_cast<Bytef*>(block);z.avail_out=sizeof(block);status=inflate(&z,Z_NO_FLUSH);
        result.append(block,qsizetype(sizeof(block)-z.avail_out));
        if(result.size()>1024LL*1024*1024){inflateEnd(&z);return fail(error,QStringLiteral("SCE decompression exceeds 1 GiB limit"));}
    }
    const bool valid=status==Z_STREAM_END&&z.avail_in==0;inflateEnd(&z);
    if(!valid)return fail(error,QStringLiteral("Invalid compressed SCE section"));
    data=std::move(result);return true;
}
// RPCS3 unself.cpp:626-817, MetadataHeader/MetadataSectionHeader in unself.h.
bool decrypt(const QByteArray& sce,const Keys& k,QByteArray& output,QString* error) {
    if(sce.size()<32||sce.left(4)!=QByteArray("SCE\0",4))return fail(error,QStringLiteral("Invalid SCE header"));
    const quint64 meta=32ULL+be32(sce,12),end=be64(sce,16);
    if(!range(meta,96,end)||end>quint64(sce.size()))return fail(error,QStringLiteral("Invalid SCE metadata bounds"));
    QByteArray info=sce.mid(qsizetype(meta),64);
    if(!(qFromBigEndian<quint16>(sce.constData()+8)&0x8000))firmware::aesCbc(info,k.erk,k.riv);
    if(info.mid(16,16)!=QByteArray(16,0)||info.mid(48,16)!=QByteArray(16,0))return fail(error,QStringLiteral("Failed to decrypt SCE metadata (wrong keys?)"));
    QByteArray metadata=sce.mid(qsizetype(meta+64),qsizetype(end-meta-64));
    firmware::aesCtr(metadata,info.left(16),info.mid(32,16));
    const quint32 count=be32(metadata,12),keyCount=be32(metadata,16);
    const quint64 keyOffset=32ULL+quint64(count)*48;
    if(count<3||!range(32,quint64(count)*48,quint64(metadata.size()))||!range(keyOffset,quint64(keyCount)*16,quint64(metadata.size())))
        return fail(error,QStringLiteral("Invalid SCE section/key table"));
    // Validate every section even though only section [2] is consumed by the installer.
    for(quint32 i=0;i<count;++i) {
        const qsizetype h=32+qsizetype(i)*48;const quint64 offset=be64(metadata,h),size=be64(metadata,h+8);
        const quint32 enc=be32(metadata,h+32),ki=be32(metadata,h+36),iv=be32(metadata,h+40),comp=be32(metadata,h+44);
        if(!range(offset,size,quint64(sce.size()))||(size&&offset<end)||size>1024ULL*1024*1024||(enc==3&&(ki>=keyCount||iv>=keyCount))||
           (enc!=1&&enc!=3)||(comp!=1&&comp!=2))return fail(error,QStringLiteral("Invalid SCE section bounds/flags/keys"));
        if(i!=2)continue;
        output=sce.mid(qsizetype(offset),qsizetype(size));
        if(enc==3)firmware::aesCtr(output,metadata.mid(qsizetype(keyOffset+quint64(ki)*16),16),metadata.mid(qsizetype(keyOffset+quint64(iv)*16),16));
        if(comp==2&&!inflateSection(output,error))return false;
    }
    return true;
}
}
bool extractPup(const QString& pupPath,const QString& destDir,QString* error) {
    if(error)error->clear();Keys k;if(!keys(k,error))return false;
    QFile file(pupPath);
    if(!file.open(QIODevice::ReadOnly))return fail(error,QStringLiteral("Cannot open PUP: %1").arg(file.errorString()));
    // Bounded in-memory parsing keeps all offsets checked before allocation/slicing.
    if(file.size()>1024LL*1024*1024)return fail(error,QStringLiteral("PUP exceeds 1 GiB limit"));
    const QByteArray pup=file.readAll();
    if(file.error()!=QFileDevice::NoError||pup.size()<48||pup.left(8)!=QByteArray("SCEUF\0\0\0",8))return fail(error,QStringLiteral("Invalid PUP header/magic"));
    const quint64 count=be64(pup,24),header=be64(pup,32),data=be64(pup,40),total=quint64(pup.size());
    if(!count||count>(total-48)/64||header<48+count*64||!range(header,data,total))return fail(error,QStringLiteral("Invalid PUP header/table lengths"));
    QMap<quint64,QByteArray> hashes;
    for(quint64 i=0;i<count;++i) {
        const qsizetype at=qsizetype(48+count*32+i*32);const quint64 id=be64(pup,at);
        if(hashes.contains(id))return fail(error,QStringLiteral("Duplicate PUP hash entry"));
        hashes.insert(id,pup.mid(at+8,20));
    }
    QByteArray update;QSet<quint64> seen;
    for(quint64 i=0;i<count;++i) {
        const qsizetype at=qsizetype(48+i*32);const quint64 id=be64(pup,at),offset=be64(pup,at+8),size=be64(pup,at+16);
        if(seen.contains(id)||!hashes.contains(id)||offset<header||!range(offset,size,header+data))return fail(error,QStringLiteral("Invalid PUP entry bounds/id"));
        seen.insert(id);const QByteArray contents=pup.mid(qsizetype(offset),qsizetype(size));
        if(firmware::hmacSha1(k.pup,contents)!=hashes.value(id))return fail(error,QStringLiteral("PUP HMAC-SHA1 mismatch for entry 0x%1").arg(id,0,16));
        if(id==0x300)update=contents;
    }
    if(!seen.contains(0x300))return fail(error,QStringLiteral("PUP update TAR entry 0x300 is missing"));
    const QString root=QDir::cleanPath(QFileInfo(destDir).absoluteFilePath());
    if(destDir.isEmpty()||!noLinks(root))return fail(error,QStringLiteral("Unsafe destination (link/reparse point)"));
    quint64 packages=0,written=0;QMap<QString,QByteArray> pending;
    // Validate/decrypt all packages and all TAR paths before writing anything.
    if(!tar(update,[&](const QString& name,char type,const QByteArray& bytes) {
        if(type=='5'||!QFileInfo(name).fileName().startsWith(QStringLiteral("dev_flash_")))return true;
        QByteArray unpacked;if(!decrypt(bytes,k,unpacked,error))return false;++packages;
        return tar(unpacked,[&](QString path,char entryType,const QByteArray& contents) {
            path=QDir::cleanPath(path);
            if(path==QStringLiteral("dev_flash")||path==QStringLiteral("."))return true;
            if(path.startsWith(QStringLiteral("dev_flash/")))path.remove(0,10);
            if(!safeName(path))return fail(error,QStringLiteral("Unsafe dev_flash path"));
            const QString target=root+'/'+path;
            if(!target.startsWith(root+'/')||!noLinks(target))return fail(error,QStringLiteral("Unsafe output path: %1").arg(path));
            if(entryType!='5') {
                written+=quint64(contents.size());
                if(written>2ULL*1024*1024*1024)return fail(error,QStringLiteral("Extracted firmware exceeds 2 GiB limit"));
                if(pending.contains(path))return fail(error,QStringLiteral("Duplicate firmware file: %1").arg(path));
                pending.insert(path,contents);
            }
            return true;
        },error);
    },error))return false;
    if(!packages||pending.isEmpty())return fail(error,QStringLiteral("PUP verified (%1 entries), but no dev_flash packages/files found").arg(count));
    if(!QDir().mkpath(root))return fail(error,QStringLiteral("Cannot create destination"));
    for(auto it=pending.cbegin();it!=pending.cend();++it) {
        const QString target=root+'/'+it.key();
        if(!noLinks(target)||!QDir().mkpath(QFileInfo(target).absolutePath()))return fail(error,QStringLiteral("Unsafe/unwritable output directory"));
        QSaveFile out(target);
        if(!out.open(QIODevice::WriteOnly)||out.write(it.value())!=it.value().size()||!out.commit())return fail(error,QStringLiteral("Cannot write %1: %2").arg(target,out.errorString()));
    }
    if(!QFileInfo::exists(root+QStringLiteral("/vsh/resource/explore_plugin_full.rco")))
        std::fprintf(stderr,"Firmware extraction warning: explore_plugin_full.rco absent (layout may differ)\n");
    std::fprintf(stdout,"Firmware extracted: %llu entries verified, %llu dev_flash packages, %llu bytes written\nDestination: %s\n",
                 static_cast<unsigned long long>(count),static_cast<unsigned long long>(packages),static_cast<unsigned long long>(written),root.toUtf8().constData());
    return true;
}
}
