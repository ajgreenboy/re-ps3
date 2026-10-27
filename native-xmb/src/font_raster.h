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
    // delta26_6 is the sub-pixel phase the firmware applies before rasterising: cellFontFT_65E27CB3
    // @ 0x6aa4 masks both phase arguments with 0x3f and calls FT_Set_Transform (@ 0x2193/0x2222),
    // so the outline is translated by that fraction of a pixel and the returned coverage and
    // bearing both carry it (audit/TEXT-RASTER.md 1.3).
    RasterGlyph rasterize(const QRawFont& font, quint32 glyphIndex, qint32 loadFlags,
                          QPoint delta26_6 = QPoint(0, 0));
    // Unhinted 26.6 advance for one glyph, in 1/64 of the font's pixel size. PAF sums whole
    // advances plus kerning (_opd_FUN_00397e5c @ 0x397e5c), so this is the metric authority the
    // native renderer keeps for placement, elision and the caret.
    qreal advance(const QRawFont& font, quint32 glyphIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xmb
