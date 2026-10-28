#pragma once
#include <QHash>
#include <QDir>
#include <QImage>
#include <QString>
#include "qrc.h"
#include <memory>
#include <unordered_map>

namespace xmb {
// A read-only asset service. Firmware bytes stay in the user's local dev_flash.
class Assets final {
public:
    explicit Assets(QString flashRoot);
    const QImage& image(const QString& container, const QString& name);
    // The largest copy of an RCO image: firmware ships many textures several times (e.g. tex_psn 46 px in
    // explore_plugin_full, 128 px in xmb_plugin_normal). Small sources (<96 px) are swapped for the largest same-name,
    // same-aspect copy in any RCO; the choice is cached.
    const QImage& bestImage(const QString& container, const QString& name);
    QString text(const QString& container,const QString& name,const QString& fallback={});
    QByteArray qglFile(const QString& archive,const QString& name);
    QString fontFamily() const { return font_; }
    QString fontFamily(int style) const { return fonts_.value(style,font_); }
    QStringList fontFamilies() const { return fonts_; }
    QString root() const { return root_; }
    // dev_hdd0 next to dev_flash unless overridden (installed games and apps).
    QString hddRoot() const { return hddRoot_.isEmpty()?QDir(root_).absoluteFilePath("../dev_hdd0"):hddRoot_; }
    void setHddRoot(const QString& path) { hddRoot_=path; }
    // Directory of locally decrypted VSH modules (*.prx); dev_flash itself holds them encrypted (*.sprx).
    void setModulesRoot(const QString& path) { modulesRoot_=path; }
    // A decrypted module's bytes, or empty when no modules directory is set or the file is missing.
    QByteArray vshModule(const QString& name) const;
    int decodedImages() const { return decodedImages_; }
    int failedImages() const { return int(images_.size())-decodedImages_; }
    // Uncompressed or RLE true-colour/grey TGA (qgl/lines LUTs and particle textures), top row first; null on error.
    static QImage decodeTga(const QByteArray& file);
    // Image names in an RCO container (local analysis).
    QStringList imageNames(const QString& container) const { return readContainer(container).entries.keys(); }
    // Every English string of an RCO container, id -> text (local analysis).
    QHash<QString,QString> allText(const QString& container) const { return readEnglish(readContainer(container)); }
private:
    struct Container { QByteArray file, tables; quint32 labels=0, imageData=0; QHash<QString,quint32> entries; };
    QString root_, font_, hddRoot_, modulesRoot_;
    QStringList fonts_;
    QHash<QString,Container> containers_;
    // Node-based so the references image() hands out stay valid while later lookups insert (QHash may move values).
    std::unordered_map<QString,QImage> images_;
    QHash<QString,QHash<QString,QString>> texts_;
    QHash<QString,std::shared_ptr<QrcArchive>> qrcs_;
    int decodedImages_=0;
    QHash<QString,QStringList> nameIndex_; // RCO image name -> containers holding it (built on first need)
    QHash<QString,QString> bestContainer_; // container/name -> container with the largest copy
    Container readContainer(const QString& name) const;
    QHash<QString,QString> readEnglish(const Container& container) const;
    // GIM decoder: RGBA8888 (0x3), DXT1/3/5 (0x8/0x9/0xA) and the indexed formats
    // INDEX4/INDEX8/INDEX16 (0x4/0x5/0x6) with the picture's palette block. Indexed
    // pictures without a palette block decode as white coverage masks (the 4.93 OSK
    // format-0x5 badge/caption textures use that form). XMB_GIM_TRACE=1 logs the
    // header, palette and result of every picture.
    static QImage decodeGim(const QByteArray& bytes,const QString& name={});
    static QImage decodeDds(const QByteArray& bytes);
};
}
