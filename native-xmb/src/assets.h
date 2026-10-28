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
    // ---- Container resolution (audit/ICON-GAP-AUDIT.md bucket B/C icon wave) -------------------------
    // An image name is not a resolution: the same name can exist in several RCOs, sometimes as different
    // artwork (tex_music is the flat note in explore_plugin_full, the wrench note in explore_category_sysconf
    // and a glass note in xmb_ingame - audit/TEXTURE-COVERAGE.md 7.1). image() therefore reads the
    // container it is given, and the only cross-container move it makes is a recorded container-override
    // rule (kContainerOverrides in assets.cpp): when the requested (container,name) is absent and a rule
    // names a container that actually ships that name, the owner's pixels are used and the migration is
    // logged once. A pair with no rule and no local copy still resolves to nothing, but it now warns
    // instead of returning silently.
    //
    // ImageHit reports where a lookup's pixels came from. iconImage() is the explicit drawing entry point
    // (same pixels as image(), plus the provenance) so an icon site can prove its container is correct.
    struct ImageHit {
        QString requestedContainer, container, name; // container: the RCO the pixels were read from
        bool migrated = false;    // container != requestedContainer
        bool knownOwner = false;  // a container-override rule named the owner
    };
    const QImage& iconImage(const QString& container, const QString& name, ImageHit* hit=nullptr);
    // Every vsh/resource RCO that holds name (sorted; local analysis, index built once).
    QStringList imageContainers(const QString& name);
    // The container-override rule's owner for a requested pair, or an empty string.
    QString knownOwner(const QString& container, const QString& name) const;
    // The override table as "requested/name -> owner (evidence)" lines, for --image-audit.
    QStringList containerOverrideRules() const;
    // The names bestImage may upgrade across containers; exactly three, each behind the pixel gate.
    QStringList sameArtAllowList() const;
    // bestImage's decision for a pair (the --image-audit table's gate column). Call bestImage first.
    struct BestDecision {
        QString requestedContainer, container;
        bool allowListed = false, gateApplied = false, upgraded = false;
        QString reason;
        int width = 0, height = 0;
    };
    BestDecision bestImageDecision(const QString& container, const QString& name) const;
    // The largest copy of an RCO image whose artwork is the same picture. Firmware ships many textures several
    // times (e.g. tex_psn 46 px in explore_plugin_full, 128 px in xmb_plugin_normal). A source smaller than 96 px
    // is upgraded to the largest same-name copy in any RCO only when the name is in the same-art allow-list AND
    // the two pictures pass a scale-independent comparison (alpha bounds, then 32x32 alpha/colour signatures).
    // The same name alone is not evidence of the same art: explore_plugin_full:tex_music is the flat music note,
    // explore_category_sysconf:tex_music is that note carrying the settings wrench and xmb_ingame:tex_music is a
    // glass note. Every decision and its reason is logged; XMB_BESTIMAGE_TRACE=1 (or a comma-separated name list)
    // prints the source, every candidate and its metrics. The choice is cached.
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
    // A GIM picture for a caller outside the RCO path (theme icons, pointers and the notification plaque). It is
    // the same decoder RCO images use, including indexed pictures with or without a palette block, so the theme
    // reader is not a second, weaker GIM parser. A payload that is not a readable GIM yields a null image and a
    // warning (with the decoder's reason) instead of throwing.
    static QImage decodeGimImage(const QByteArray& bytes);
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
    QHash<QString,QString> bestContainer_; // container/name -> equivalent container chosen (source when none)
    QHash<QString,BestDecision> bestDecisions_; // container/name -> bestImage's last decision (audit)
    Container readContainer(const QString& name) const;
    // The one lookup body behind image()/iconImage(): requested container, container-override rule, cache.
    const QImage& loadImage(const QString& container,const QString& name,ImageHit* hit);
    // Where a pair's pixels come from, without decoding: the requested container when it holds the name,
    // else the override rule's owner when that owner holds it, else the requested container.
    ImageHit hitFor(const QString& container,const QString& name) const;
    void ensureNameIndex();
    QHash<QString,QString> readEnglish(const Container& container) const;
    // GIM decoder: RGBA8888 (0x3), DXT1/3/5 (0x8/0x9/0xA) and the indexed formats
    // INDEX4/INDEX8/INDEX16 (0x4/0x5/0x6) with the picture's palette block. Indexed
    // pictures without a palette block decode as white coverage masks (the 4.93 OSK
    // format-0x5 badge/caption textures and user-theme masks use that form).
    // XMB_GIM_TRACE=1 logs the header, palette and result of every picture.
    static QImage decodeGim(const QByteArray& bytes,const QString& name={});
    static QImage decodeDds(const QByteArray& bytes);
};
}
