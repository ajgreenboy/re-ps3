#pragma once
#include <QColor>
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace xmb {
class Assets;
class RcoObjects;

// The action a key performs. The view dispatches on this instead of the old private OskAction enum.
// Character means "insert label"; MoveUp/MoveDown move the key focus; CaretLeft/CaretRight move the
// text caret (the L1/R1 badges); Mode is the firmware's Input Mode key, which the native build logs
// as unavailable (audit section 8.3).
enum class OskAction {
    None, Character, Backspace, Space, Mode, Shift, Palette, Enter, Accent,
    MoveUp, MoveDown, CaretLeft, CaretRight, Copy, Paste
};
QString oskActionName(OskAction action);

// One key of one page. column is the left cell; span is the number of 72x54 cells the key covers.
// face is the draw role ("letter", "olive", "blue", "mint", "arrow"); faceTexture is the exact RCO
// image name when the firmware/module assigns one. badge is the text fallback and badgeTexture the
// LABEL_* image the capture shows. A spacer key is blank, draws its face and does nothing.
struct OskKey {
    int row = 0;
    int column = 0;
    int span = 1;
    QString label;
    QString icon;
    OskAction action = OskAction::None;
    QString face;
    QString faceTexture;
    QString badge;
    QString badgeTexture;
    bool blank = true;
};

struct OskRow {
    int row = 0;
    QVector<OskKey> keys;
    const OskKey* keyAt(int column) const;
};

struct OskPage {
    QString name;
    QString modeCaption;      // prediction mode cell: "a" on the lower page, "A" on the caps page
    QString provenance;       // one line: character rows vs function rows
    QVector<OskRow> rows;     // 6 rows: 4 character rows and 2 function rows
    const OskRow* rowAt(int row) const;
    const OskKey* keyAt(int row, int column) const;
};

// The symbol palette is a 7x7 page of 49 buttons over a scrollable symbol list. The module string
// carries a one-character page flag followed by the symbols; the flag is exposed separately and is
// not drawn. The 7x7 page and 49 buttons are cross-checked against symbol_palette_bg (width/height
// 7x7) and the 49 sym_btnNN children of symbol_palette_group.
struct OskPalette {
    QRect frame;
    QPoint gridOrigin;
    QSize cell;
    QSize pitch;
    int columns = 7;
    int rows = 7;
    int visibleRows = 5;
    int buttonCount = 49;
    QChar pageFlag;
    QVector<QChar> symbols;
    QString extras;           // the accented symbols that follow the ASCII run in the module string
    QString provenance;
    QChar symbolAt(int index) const;
    QChar symbolAtCell(int row, int column, int scrollRow = 0) const;
    QRect cellRect(int row, int column) const;
};

struct OskCaptions {
    QString space;
    QString backspace;
    QString inputMode;
    QString enter;
    QString prediction;
    QString modeLower;
    QString modeCaps;
    QString source;
    bool complete = false;
};

// The RCO Text object's own material (Materials::textMaterial): size, top/bottom colour, spacing
// and shadow. fullkey_list_text carries size=10, white, shadow 4/-5 at 0.25 (audit section 2.1).
struct OskTextStyle {
    bool present = false;
    double size = 10;
    QColor top = QColor(255, 255, 255);
    QColor bottom = QColor(255, 255, 255);
    QColor shadow = QColor(64, 64, 64, 255);
    double shadowX = 4;
    double shadowY = -5;
    double shadowAlpha = 1;
    QString fontReference;
    QString source;
};

struct OskCaret {
    QColor colour = QColor(130, 249, 12);
    int height = 27;
    int shaftWidth = 2;
    int serifWidth = 12;
    int markerWidth = 14;
    int markerHeight = 20;
    int markerInset = 12;
    // One blink phase, not the full period: the 2026-10-28 capture batch measured the caret lit for
    // 495/504/498 ms and dark for 442/431 ms (period ~936 ms, duty ~53%), and the draw alternates
    // phases every blinkMilliseconds, so the mean phase is ~468 ms.
    int blinkMilliseconds = 468;
    bool blinkTraced = true;   // capture batch 2026-10-28, audit/OSK-FIRMWARE.md section 13.1
};

struct OskMask {
    QChar glyph = QChar(0x2022);
    bool oneBulletPerCharacter = true;
    bool caretAfterLastBullet = true;
    QString source;             // shape only; metrics unverified (audit 5.4)
};

struct OskBands {
    QRect panel;
    QRect titleBar;
    QRect titleIcon;
    QRect titleText;
    QRect field;
    QRect footer;
    QRect candidateList;
    QRect predictionHeader;
    QRect modeCell;
    QRect countCell;
    QRect copyButton;
    QRect pasteButton;
    QColor titleTop = QColor(141, 142, 140);
    QColor titleBottom = QColor(140, 146, 140);
    QColor fieldTop = QColor(86, 86, 86);
    QColor fieldMid = QColor(62, 62, 62);
    QColor fieldBottom = QColor(47, 47, 47);
    QColor footerTop = QColor(70, 70, 70);
    QColor footerBottom = QColor(95, 96, 94);
    QColor titleTextColour = QColor(250, 250, 250);
    QColor fieldTextColour = QColor(255, 255, 255);
    QColor dim = QColor(0, 0, 0);
    double dimAlpha = 0.70;
};

struct OskTexture {
    QString name;
    QString role;
    QSize size;
    bool present = false;      // the name exists in the RCO image table
    bool decoded = false;      // Assets::image returned pixels (some GIM pixel formats are not decoded)
    bool defaultSurface = true;
};

// The prediction column's two buttons sit outside the 10-column grid. They are kept as their own
// list so the draw slice can hit-test them without inventing fake grid columns.
struct OskPredictionButton {
    QString name;
    QRect rect;
    OskAction action = OskAction::None;
    QString texture;
    bool enabled = true;
};

struct OskCost {
    qint64 rcoNs = 0;
    qint64 textNs = 0;
    qint64 moduleNs = 0;
    qint64 textureNs = 0;
    qint64 totalNs = 0;
    int rcoBytes = 0;
    int moduleBytes = 0;
    int texturesResolved = 0;  // names present in the image table
    int texturesDecoded = 0;   // pixels returned through Assets::image
    double totalMs() const { return double(totalNs) / 1.0e6; }
};

struct OskAssertion {
    QString name;
    bool ok = false;
    bool applicable = true;
    QString detail;
};

enum class OskLayoutSource { Firmware, Committed };

// The resolved keyboard layout. Rectangles are in the 1920x1080 reference space. Every source is
// recorded: the RCO supplies object/image references, captions and the 7x7 palette grid; the module
// supplies the row strings and the palette symbols; the measured reference supplies the panel bands
// and grid origin because the RCO's panel planes store width=0 and are assigned at runtime.
struct OskLayout {
    bool loaded = false;
    bool firmware = false;
    bool moduleRows = false;
    bool paletteFromModule = false;
    bool captionsFromRco = false;
    bool texturesResolved = false;   // all 52 names present in the RCO image table
    bool texturesDecoded = false;    // all 52 returned pixels through Assets::image
    OskLayoutSource source = OskLayoutSource::Committed;
    QString provenance;
    QStringList provenanceChain;
    QHash<QString, QString> anchors;   // object/property anchors quoted from the RCO

    OskBands bands;
    QVector<OskPredictionButton> predictionButtons;   // copy, paste
    QPoint gridOrigin;
    QSize cell = QSize(72, 54);
    QSize face = QSize(66, 50);
    QPoint faceInset = QPoint(3, 2);
    int columns = 10;
    int rows = 6;
    int keySeam = 6;
    int keyCornerRadius = 4;
    int defaultFocusRow = 2;
    int defaultFocusColumn = 4;   // 0-based; the capture/audit call this column 5 counting from 1

    QVector<OskPage> pages;            // lower, caps
    QStringList pageNames;             // lower, caps, symbolPalette
    OskPalette palette;
    OskCaptions captions;
    OskTextStyle textStyle;
    OskCaret caret;
    OskMask mask;

    QVector<OskTexture> textures;      // 52 named textures
    QStringList auxiliaryTextures;     // the 4 that belong to the menu/notify/copy/pointer pages
    QHash<QString, QString> slices;    // "panel/1x0" -> "tbase1x0", "candidate/0x0" -> ...
    QStringList panelSlices;
    QStringList candidateSlices;
    QStringList menuSlices;
    QSize buttonBase1Col = QSize(72, 54);
    QSize buttonBase2Col = QSize(144, 54);
    QSize pointerSize = QSize(40, 47);

    int keyCount = 0;
    int characterKeyCount = 0;
    int row4KeyCount = 0;
    int row5KeyCount = 0;
    int defaultSurfaceTextureCount = 0;
    OskCost cost;

    const OskPage* page(int index) const;
    const OskPage* page(const QString& name) const;
    const OskKey* keyAt(int pageIndex, int row, int column) const;
    QRect cellRect(int pageIndex, int row, int column) const;
    QRect keyFaceRect(int pageIndex, int row, int column) const;
    QRect paletteCellRect(int row, int column) const;

    QString describe() const;
    QVector<OskAssertion> assertions() const;
    bool passesAssertions() const;
};

// Load the OSK layout from the firmware through the existing readers:
//   - oskfullkeypanel_plugin.rco via RcoObjects (objects, image references, text table, 7x7 palette
//     grid, 49 palette buttons, Text material via Materials::textMaterial);
//   - oskfullkeypanel_plugin.prx via Assets::vshModule (the decrypted module set by --vsh-modules):
//     the 24-byte label table, the English lower/caps row groups and the palette string;
//   - the 52 named textures through Assets::image (the same path View::drawImage uses).
// With no dev-flash root the committed table (work/osk-layout.proposed.json) is used and the
// provenance line says so. A firmware read that fails falls back the same way, loudly. The function
// logs one provenance line; XMB_OSK_LAYOUT_LOG=1 additionally prints the resolved layout and every
// assertion. It does no GL work and owns no widgets.
OskLayout loadOskLayout(Assets& assets, RcoObjects& objects);

} // namespace xmb
