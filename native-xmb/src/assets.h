#pragma once
#include <QHash>
#include <QDir>
#include <QImage>
#include <QString>
#include "qrc.h"
#include <memory>

namespace xmb {
// A read-only asset service. Firmware bytes stay in the user's local dev_flash.
class Assets final {
public:
    explicit Assets(QString flashRoot);
    const QImage& image(const QString& container, const QString& name);
    QString text(const QString& container,const QString& name,const QString& fallback={});
    QByteArray qglFile(const QString& archive,const QString& name);
    QString fontFamily() const { return font_; }
    QString fontFamily(int style) const { return fonts_.value(style,font_); }
    QStringList fontFamilies() const { return fonts_; }
    QString root() const { return root_; }
    // dev_hdd0 next to dev_flash unless overridden (installed games and apps).
    QString hddRoot() const { return hddRoot_.isEmpty()?QDir(root_).absoluteFilePath("../dev_hdd0"):hddRoot_; }
    void setHddRoot(const QString& path) { hddRoot_=path; }
    int decodedImages() const { return decodedImages_; }
    int failedImages() const { return images_.size()-decodedImages_; }
    // Uncompressed or RLE true-colour/grey TGA (qgl/lines LUTs and particle textures), top row first; null on error.
    static QImage decodeTga(const QByteArray& file);
    // Image names in an RCO container (local analysis).
    QStringList imageNames(const QString& container) const { return readContainer(container).entries.keys(); }
    // Every English string of an RCO container, id -> text (local analysis).
    QHash<QString,QString> allText(const QString& container) const { return readEnglish(readContainer(container)); }
private:
    struct Container { QByteArray file, tables; quint32 labels=0, imageData=0; QHash<QString,quint32> entries; };
    QString root_, font_, hddRoot_;
    QStringList fonts_;
    QHash<QString,Container> containers_;
    QHash<QString,QImage> images_;
    QHash<QString,QHash<QString,QString>> texts_;
    QHash<QString,std::shared_ptr<QrcArchive>> qrcs_;
    int decodedImages_=0;
    Container readContainer(const QString& name) const;
    QHash<QString,QString> readEnglish(const Container& container) const;
    static QImage decodeGim(const QByteArray& bytes);
    static QImage decodeDds(const QByteArray& bytes);
};
}
