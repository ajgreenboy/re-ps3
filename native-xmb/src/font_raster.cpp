#include "font_raster.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRawFont>
#include <QVector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace xmb {
namespace {
constexpr qint64 maxFontBytes = 32 * 1024 * 1024;
constexpr qreal maxPixelSize = 4096.0;

QByteArray sfntTable(FT_Face face, FT_ULong tag)
{
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) != 0 || length == 0 ||
        length > static_cast<FT_ULong>(std::numeric_limits<int>::max()))
        return {};
    QByteArray bytes(static_cast<int>(length), Qt::Uninitialized);
    if (FT_Load_Sfnt_Table(face, tag, 0,
                           reinterpret_cast<FT_Byte*>(bytes.data()), &length) != 0)
        return {};
    return bytes;
}

QByteArray rawFontTable(const QRawFont& font, const char* tag)
{
    return font.fontTable(tag);
}
}

struct FontRaster::Impl {
    struct Face {
        QByteArray bytes; // FT_New_Memory_Face requires storage to outlive FT_Face.
        FT_Face face = nullptr;
        QString path;
        QByteArray head;
        QByteArray cmap;
        FT_F26Dot6 size64 = 0;
        QPoint delta26_6; // transform currently installed on the face, in 1/64 pixel
    };

    FT_Library library = nullptr;
    QVector<Face> faces;
    QHash<QByteArray, int> digestLookup;
    mutable QVector<QPair<QRawFont,int>> matches;
    QString freetypeVersion;

    ~Impl()
    {
        clearFaces();
        if (library)
            FT_Done_FreeType(library);
    }

    void clearFaces()
    {
        for (Face& entry : faces) {
            if (entry.face)
                FT_Done_Face(entry.face);
            entry.face = nullptr;
        }
        faces.clear();
        digestLookup.clear();
        matches.clear();
    }

    QByteArray digest(const QByteArray& head, const QByteArray& cmap) const
    {
        // Include lengths so the pair has an unambiguous cache key.
        QByteArray key;
        key.reserve(16 + head.size() + cmap.size());
        const quint64 headLength = quint64(head.size());
        const quint64 cmapLength = quint64(cmap.size());
        key.append(reinterpret_cast<const char*>(&headLength), sizeof(headLength));
        key.append(head);
        key.append(reinterpret_cast<const char*>(&cmapLength), sizeof(cmapLength));
        key.append(cmap);
        return QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    }

    int match(const QRawFont& font) const
    {
        if (!font.isValid())
            return -1;
        for(const auto& cached:matches)if(cached.first==font)return cached.second;
        const QByteArray head = rawFontTable(font, "head");
        const QByteArray cmap = rawFontTable(font, "cmap");
        if (head.isEmpty() || cmap.isEmpty())
            return -1;
        const QByteArray key = digest(head, cmap);
        const auto cached = digestLookup.constFind(key);
        if (cached != digestLookup.cend()) {
            const int index = cached.value();
            if (index >= 0 && index < faces.size() && faces[index].head == head &&
                faces[index].cmap == cmap)
                return remember(font,index);
        }
        for (int i = 0; i < faces.size(); ++i) {
            if (faces[i].head == head && faces[i].cmap == cmap)
                return remember(font,i);
        }
        return remember(font,-1);
    }

    int remember(const QRawFont& font,int index) const
    {
        // CJK cmap tables can be large. Resolve identity once per sized face, not for every glyph.
        if(matches.size()>=128)matches.removeFirst();
        matches.append({font,index});
        return index;
    }
};

FontRaster::FontRaster() : impl_(std::make_unique<Impl>())
{
    if (FT_Init_FreeType(&impl_->library) == 0 && impl_->library) {
        FT_Int major = 0, minor = 0, patch = 0;
        FT_Library_Version(impl_->library, &major, &minor, &patch);
        impl_->freetypeVersion = QStringLiteral("FreeType %1.%2.%3").arg(major).arg(minor).arg(patch);
    }
}

FontRaster::~FontRaster() = default;
FontRaster::FontRaster(FontRaster&&) noexcept = default;
FontRaster& FontRaster::operator=(FontRaster&&) noexcept = default;

bool FontRaster::configure(const QString& fontDirectory)
{
    if (!impl_ || !impl_->library)
        return false;
    impl_->clearFaces();
    QDir directory(fontDirectory);
    if (!directory.exists())
        return false;

    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("SCE-PS3-*.TTF"), QStringLiteral("sce-ps3-*.ttf")},
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files) {
        const qint64 size = info.size();
        if (size <= 0 || size > maxFontBytes || size > std::numeric_limits<FT_Long>::max())
            continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QByteArray bytes = file.readAll();
        if (bytes.size() != size || bytes.isEmpty() || bytes.size() > maxFontBytes)
            continue;

        FT_Face face = nullptr;
        if (FT_New_Memory_Face(impl_->library,
                               reinterpret_cast<const FT_Byte*>(bytes.constData()),
                               static_cast<FT_Long>(bytes.size()), 0, &face) != 0 || !face)
            continue;
        Impl::Face entry;
        entry.bytes = std::move(bytes);
        entry.face = face;
        entry.path = info.absoluteFilePath();
        entry.head = sfntTable(face, FT_MAKE_TAG('h', 'e', 'a', 'd'));
        entry.cmap = sfntTable(face, FT_MAKE_TAG('c', 'm', 'a', 'p'));
        if (entry.head.isEmpty() || entry.cmap.isEmpty()) {
            FT_Done_Face(face);
            continue;
        }
        const int index = impl_->faces.size();
        impl_->digestLookup.insert(impl_->digest(entry.head, entry.cmap), index);
        impl_->faces.append(std::move(entry));
    }
    return !impl_->faces.isEmpty();
}

QString FontRaster::version() const
{
    return impl_ ? impl_->freetypeVersion : QString();
}

QString FontRaster::sourceFor(const QRawFont& font) const
{
    if (!impl_)
        return {};
    const int index = impl_->match(font);
    return index >= 0 ? impl_->faces[index].path : QString();
}

QString FontRaster::mapSymbols(QString text)
{
    text.replace(QChar(0xf8fc),QChar(0x3007));
    text.replace(QChar(0xf8fd),QChar(0x00d7));
    text.replace(QChar(0xf8fe),QChar(0x25a2));
    text.replace(QChar(0xf8ff),QChar(0x25a1));
    text.replace(QChar(0x2715),QChar(0x00d7));
    return text;
}

FontRaster::RasterGlyph FontRaster::rasterize(const QRawFont& font, quint32 glyphIndex,
                                              qint32 loadFlags, QPoint delta26_6)
{
    RasterGlyph result;
    if (!impl_ || !impl_->library || !font.isValid())
        return result;
    const int index = impl_->match(font);
    if (index < 0)
        return result;
    const qreal pixelSize = font.pixelSize();
    if (!std::isfinite(pixelSize) || pixelSize <= 0.0 || pixelSize > maxPixelSize)
        return result;
    const qint64 size64 = qRound64(pixelSize * 64.0);
    if (size64 <= 0 || size64 > 4096 * 64)
        return result;

    auto& entry=impl_->faces[index];
    FT_Face face = entry.face;
    if(entry.size64!=size64) {
        if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(size64), 72, 72) != 0)return result;
        entry.size64=static_cast<FT_F26Dot6>(size64);
    }
    // Sub-pixel phase. The firmware's draw function masks both pen phases with 0x3f and calls
    // FT_Set_Transform before FT_Load_Glyph, so the outline itself moves by that fraction and the
    // rendered coverage carries it. Reinstalled only when the phase changes: the transform lives
    // on the face and every rasterisation below states its own delta, so the result is
    // independent of call order.
    if(entry.delta26_6!=delta26_6) {
        FT_Matrix identity = {0x10000L, 0, 0, 0x10000L};
        FT_Vector delta = {static_cast<FT_Pos>(delta26_6.x()), static_cast<FT_Pos>(delta26_6.y())};
        FT_Set_Transform(face, &identity, &delta);
        entry.delta26_6 = delta26_6;
    }
    const FT_Int32 flags = static_cast<FT_Int32>(loadFlags) | FT_LOAD_NO_BITMAP;
    if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyphIndex), flags) != 0 ||
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        return result;

    const FT_Bitmap& bitmap = face->glyph->bitmap;
    if (bitmap.width > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        bitmap.rows > static_cast<unsigned int>(std::numeric_limits<int>::max()))
        return result;
    result.coverage = QImage(static_cast<int>(bitmap.width), static_cast<int>(bitmap.rows),
                             QImage::Format_Grayscale8);
    if (result.coverage.isNull() && bitmap.width != 0 && bitmap.rows != 0)
        return {};
    result.coverage.fill(0);

    const int pitch = bitmap.pitch;
    const int absPitch = pitch < 0 ? -pitch : pitch;
    for (unsigned int y = 0; y < bitmap.rows; ++y) {
        const unsigned int sourceY = pitch < 0 ? bitmap.rows - 1 - y : y;
        const unsigned char* source = bitmap.buffer + size_t(sourceY) * size_t(absPitch);
        uchar* destination = result.coverage.scanLine(static_cast<int>(y));
        for (unsigned int x = 0; x < bitmap.width; ++x) {
            if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                const unsigned int value = source[x];
                const unsigned int levels = bitmap.num_grays;
                destination[x] = static_cast<uchar>(levels > 1
                    ? std::min(255U, (value * 255U + (levels - 1) / 2U) / (levels - 1))
                    : value);
            } else if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                destination[x] = (source[x >> 3] & (0x80U >> (x & 7U))) ? 255 : 0;
            } else {
                return {};
            }
        }
    }
    result.bearing = QPoint(face->glyph->bitmap_left, -face->glyph->bitmap_top);
    result.valid = true;
    return result;
}

qreal FontRaster::advance(const QRawFont& font, quint32 glyphIndex) const
{
    if (!impl_ || !impl_->library || !font.isValid())
        return 0;
    const int index = impl_->match(font);
    if (index < 0)
        return 0;
    const qreal pixelSize = font.pixelSize();
    if (!std::isfinite(pixelSize) || pixelSize <= 0.0 || pixelSize > maxPixelSize)
        return 0;
    const qint64 size64 = qRound64(pixelSize * 64.0);
    if (size64 <= 0 || size64 > 4096 * 64)
        return 0;
    auto& entry = impl_->faces[index];
    if (entry.size64 != size64) {
        if (FT_Set_Char_Size(entry.face, 0, static_cast<FT_F26Dot6>(size64), 72, 72) != 0)
            return 0;
        entry.size64 = static_cast<FT_F26Dot6>(size64);
    }
    // A translation transform does not change the advance, but the face may still carry one from
    // the last rasterisation. Clear it so the metric call is independent of draw order.
    if (!entry.delta26_6.isNull()) {
        FT_Matrix identity = {0x10000L, 0, 0, 0x10000L};
        FT_Vector delta = {0, 0};
        FT_Set_Transform(entry.face, &identity, &delta);
        entry.delta26_6 = QPoint();
    }
    // FT_LOAD_NO_HINTING matches the firmware's load flags 10 (audit/TEXT-RASTER.md 1.1); PAF sums
    // unhinted 26.6 advances (_opd_FUN_00397e5c @ 0x397e5c).
    if (FT_Load_Glyph(entry.face, static_cast<FT_UInt>(glyphIndex),
                      FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
        return 0;
    return qreal(entry.face->glyph->advance.x) / 64.0;
}

} // namespace xmb
