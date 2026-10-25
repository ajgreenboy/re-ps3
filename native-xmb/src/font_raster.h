#pragma once

#include <QImage>
#include <QPoint>
#include <QRawFont>
#include <QString>
#include <memory>

namespace xmb {

class FontRaster {
public:
    struct RasterGlyph {
        QImage coverage;
        QPoint bearing;
        bool valid = false;
    };

    FontRaster();
    ~FontRaster();
    FontRaster(FontRaster&&) noexcept;
    FontRaster& operator=(FontRaster&&) noexcept;
    FontRaster(const FontRaster&) = delete;
    FontRaster& operator=(const FontRaster&) = delete;

    bool configure(const QString& fontDirectory);
    QString version() const;
    QString sourceFor(const QRawFont& font) const;
    // PAF's private control codes (FUN_0037a774); U+2715 is the native UI's legacy cross alias.
    static QString mapSymbols(QString text);
    RasterGlyph rasterize(const QRawFont& font, quint32 glyphIndex, qint32 loadFlags);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xmb
