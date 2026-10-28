#include "osk_layout.h"
#include "assets.h"
#include "rco_objects.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QtEndian>
#include <algorithm>

namespace xmb {
namespace {

constexpr int kMaxModuleBytes = 64 * 1024 * 1024;
constexpr int kMaxProgramHeaders = 64;
constexpr int kMaxLoadSegments = 32;
constexpr int kMaxStringUnits = 64;
constexpr int kMaxPaletteUnits = 512;
constexpr int kMaxLabelRecords = 65536;
constexpr int kMaxLabelGroups = 512;
constexpr int kMaxPaletteCandidates = 65536;
constexpr qint64 kPaletteHint = 0x40c7a;   // audit 3.1; validated, then a bounded scan is the fallback

quint16 be16(const QByteArray& b, int p) {
    return quint16((quint16(quint8(b.at(p))) << 8) | quint16(quint8(b.at(p + 1))));
}
quint32 be32(const QByteArray& b, int p) {
    return (quint32(quint8(b.at(p))) << 24) | (quint32(quint8(b.at(p + 1))) << 16) |
           (quint32(quint8(b.at(p + 2))) << 8) | quint32(quint8(b.at(p + 3)));
}
quint64 be64(const QByteArray& b, int p) {
    return (quint64(be32(b, p)) << 32) | quint64(be32(b, p + 4));
}
bool hasBytes(const QByteArray& b, qint64 p, qint64 n) {
    return p >= 0 && n >= 0 && p <= qint64(b.size()) && n <= qint64(b.size()) - p;
}

struct ModuleSegment {
    quint64 offset = 0;
    quint64 vaddr = 0;
    quint64 filesz = 0;
};
struct ModuleSegments {
    QVector<ModuleSegment> loads;
    QString error;
    bool ok = false;
};
struct LabelRecord {
    int offset = 0;
    quint32 vaddr = 0;
    quint32 id = 0;
    QString text;
};
struct LabelGroup {
    int offset = 0;
    QVector<quint32> ids;
    QStringList rows;
};
struct PaletteText {
    quint64 offset = 0;
    QChar flag;
    QVector<QChar> symbols;
    QString extras;
};

bool parseModule(const QByteArray& bytes, ModuleSegments& out) {
    out = ModuleSegments{};
    if (bytes.size() < 0x40) {
        out.error = QStringLiteral("shorter than an ELF64 header");
        return false;
    }
    if (bytes.size() > kMaxModuleBytes) {
        out.error = QStringLiteral("larger than 64 MiB");
        return false;
    }
    if (quint8(bytes.at(0)) != 0x7f || bytes.at(1) != 'E' || bytes.at(2) != 'L' || bytes.at(3) != 'F') {
        out.error = QStringLiteral("not an ELF (a dev_flash .sprx is encrypted; pass --vsh-modules with the decrypted .prx)");
        return false;
    }
    if (quint8(bytes.at(4)) != 2 || quint8(bytes.at(5)) != 2) {
        out.error = QStringLiteral("not an ELF64 big-endian module");
        return false;
    }
    const int phoff = int(be64(bytes, 0x20));
    const int phentsize = int(be16(bytes, 0x36));
    const int phnum = int(be16(bytes, 0x38));
    if (phentsize != 0x38 || phnum <= 0 || phnum > kMaxProgramHeaders) {
        out.error = QStringLiteral("unsupported program-header table");
        return false;
    }
    if (!hasBytes(bytes, phoff, qint64(phnum) * phentsize)) {
        out.error = QStringLiteral("program-header table outside the file");
        return false;
    }
    for (int i = 0; i < phnum && out.loads.size() < kMaxLoadSegments; ++i) {
        const int at = phoff + i * phentsize;
        if (be32(bytes, at) != 1) continue;   // PT_LOAD only
        const quint64 fileOffset = be64(bytes, at + 8);
        const quint64 vaddr = be64(bytes, at + 0x10);
        const quint64 filesz = be64(bytes, at + 0x20);
        if (filesz == 0) continue;
        if (fileOffset > quint64(bytes.size()) || filesz > quint64(bytes.size()) - fileOffset) {
            out.error = QStringLiteral("PT_LOAD segment outside the file");
            out.loads.clear();
            return false;
        }
        ModuleSegment segment;
        segment.offset = fileOffset;
        segment.vaddr = vaddr;
        segment.filesz = filesz;
        out.loads.push_back(segment);
    }
    if (out.loads.isEmpty()) {
        out.error = QStringLiteral("no PT_LOAD segment");
        return false;
    }
    out.ok = true;
    return true;
}

qint64 moduleOffsetOf(const ModuleSegments& segments, quint64 vaddr) {
    for (const ModuleSegment& segment : segments.loads) {
        if (vaddr >= segment.vaddr && vaddr < segment.vaddr + segment.filesz)
            return qint64(segment.offset + (vaddr - segment.vaddr));
    }
    return -1;
}

QString readModuleUtf16(const QByteArray& bytes, const ModuleSegments& segments, quint64 vaddr, int maxUnits, bool& ok) {
    ok = false;
    if (vaddr == 0) {
        ok = true;
        return QString();
    }
    const qint64 offset = moduleOffsetOf(segments, vaddr);
    if (offset < 0) return QString();
    QString value;
    value.reserve(maxUnits);
    for (int i = 0; i < maxUnits; ++i) {
        const qint64 p = offset + qint64(i) * 2;
        if (!hasBytes(bytes, p, 2)) return QString();
        const quint16 unit = be16(bytes, int(p));
        if (unit == 0) {
            ok = true;
            return value;
        }
        value.append(QChar(unit));
    }
    return QString();
}

QString moduleString(const QByteArray& bytes, const ModuleSegments& segments, quint32 vaddr, int maxUnits, bool& ok) {
    return readModuleUtf16(bytes, segments, quint64(vaddr), maxUnits, ok);
}

QVector<LabelGroup> scanLabelGroups(const QByteArray& bytes, const ModuleSegments& segments) {
    QVector<LabelRecord> records;
    records.reserve(256);
    QHash<int, LabelRecord> byOffset;
    for (int p = 0; p + 24 <= bytes.size() && records.size() < kMaxLabelRecords; p += 4) {
        if (be32(bytes, p + 4) != 0 || be32(bytes, p + 12) != 1 || be32(bytes, p + 16) != 1 ||
            be32(bytes, p + 20) != 0)
            continue;
        const quint32 id = be32(bytes, p + 8);
        if (id < 0x100 || id > 0x400 || (id & 3u) != 0) continue;
        const quint32 vaddr = be32(bytes, p);
        bool ok = false;
        QString text = moduleString(bytes, segments, vaddr, kMaxStringUnits, ok);
        if (!ok || text.size() > 40) continue;
        bool printable = true;
        for (const QChar c : text) {
            if (c.unicode() < 0x20) {
                printable = false;
                break;
            }
        }
        if (!printable) continue;
        LabelRecord record;
        record.offset = p;
        record.vaddr = vaddr;
        record.id = id;
        record.text = text;
        records.push_back(record);
        byOffset.insert(p, record);
    }

    QVector<LabelGroup> groups;
    for (const LabelRecord& first : records) {
        if (groups.size() >= kMaxLabelGroups) break;
        QVector<LabelRecord> run;
        run.reserve(5);
        bool contiguous = true;
        for (int k = 0; k < 5; ++k) {
            const auto it = byOffset.constFind(first.offset + k * 24);
            if (it == byOffset.cend()) {
                contiguous = false;
                break;
            }
            run.push_back(it.value());
        }
        if (!contiguous) continue;
        bool idsStep = true;
        for (int k = 1; k < run.size(); ++k) {
            if (run[k - 1].id + 4 != run[k].id) {
                idsStep = false;
                break;
            }
        }
        if (!idsStep) continue;
        bool rowsOk = true;
        for (int k = 0; k < 4; ++k) {
            if (run[k].text.size() != 10) {
                rowsOk = false;
                break;
            }
        }
        if (!rowsOk || !run[4].text.isEmpty()) continue;
        LabelGroup group;
        group.offset = first.offset;
        group.ids.reserve(5);
        for (const LabelRecord& record : run) group.ids.push_back(record.id);
        for (int k = 0; k < 4; ++k) group.rows.push_back(run[k].text);
        groups.push_back(group);
    }
    return groups;
}

bool paletteLike(const QString& s, int from, PaletteText& out) {
    if (s.size() < 32) return false;
    if (s.at(0).unicode() < 0x20 || s.at(0).unicode() > 0x7e) return false;
    int end = 1;
    while (end < s.size()) {
        const ushort c = s.at(end).unicode();
        if (c < 0x21 || c > 0x7e) break;
        if (end > 1 && c <= s.at(end - 1).unicode()) break;
        ++end;
    }
    const int count = end - 1;
    if (count < 30) return false;
    out.flag = s.at(0);
    out.symbols.clear();
    out.symbols.reserve(count);
    for (int i = 1; i < end; ++i) out.symbols.push_back(s.at(i));
    out.extras = s.mid(end);
    Q_UNUSED(from);
    return true;
}

bool paletteAtFileOffset(const QByteArray& bytes, qint64 offset, PaletteText& out) {
    if (!hasBytes(bytes, offset, 2)) return false;
    QString value;
    value.reserve(128);
    for (int i = 0; i < kMaxPaletteUnits; ++i) {
        const qint64 p = offset + qint64(i) * 2;
        if (!hasBytes(bytes, p, 2)) return false;
        const quint16 unit = be16(bytes, int(p));
        if (unit == 0) break;
        value.append(QChar(unit));
    }
    if (!paletteLike(value, 0, out)) return false;
    out.offset = quint64(offset);
    return true;
}

bool scanPaletteText(const QByteArray& bytes, const ModuleSegments& segments, PaletteText& out) {
    // The audited offset is a hint, not a hard-coded value: the string still has to satisfy the
    // page-flag + ascending ASCII symbol shape before it is accepted. A full bounded scan follows.
    if (paletteAtFileOffset(bytes, kPaletteHint, out)) return true;
    int candidates = 0;
    for (const ModuleSegment& segment : segments.loads) {
        const qint64 begin = qint64(segment.offset);
        const qint64 end = qint64(segment.offset + segment.filesz);
        for (qint64 p = begin; p + 2 <= end && candidates < kMaxPaletteCandidates; p += 2) {
            if (quint8(bytes.at(int(p))) != 0) continue;
            const quint8 low = quint8(bytes.at(int(p + 1)));
            if (low < 0x20 || low > 0x7e) continue;
            QString value;
            value.reserve(128);
            for (qint64 q = p; q + 2 <= end && value.size() < kMaxPaletteUnits; q += 2) {
                const quint16 unit = be16(bytes, int(q));
                if (unit == 0) break;
                value.append(QChar(unit));
            }
            if (value.size() < 32 || value.size() > kMaxPaletteUnits) continue;
            ++candidates;
            PaletteText found;
            found.offset = quint64(p);
            if (paletteLike(value, int(p), found)) {
                out = found;
                return true;
            }
        }
    }
    return false;
}

// ---- RCO helpers ---------------------------------------------------------------------------------

QString imageRef(const RcoObjects::Container& container, const RcoObjects::Object* object) {
    if (!object) return QString();
    RcoObjects::Value value;
    if (!container.property(*object, QStringLiteral("image"), value)) return QString();
    if (value.refKind() != RcoObjects::RefKind::Image) return QString();
    const QString name = container.imageName(value.target);
    return name.isEmpty() ? container.resolve(value) : name;
}

QString objectPage(const RcoObjects::Container& container, const RcoObjects::Object& object) {
    const RcoObjects::Object* current = &object;
    int depth = 0;
    while (current && depth++ < 128) {
        if (current->type == QStringLiteral("Page")) return current->name;
        current = container.parent(*current);
    }
    return QString();
}

// ---- The committed table (fallback only) ----------------------------------------------------------

QString accentLabel() {
    return QString(QChar('a')) + QChar('/') + QChar(0x00e0);
}

QString squareBadge() { return QString(QChar(0x25a1)); }
QString triangleBadge() { return QString(QChar(0x25b3)); }
QString upBadge() { return QString(QChar(0x25b2)); }
QString downBadge() { return QString(QChar(0x25bc)); }
QString leftBadge() { return QString(QChar(0x25c0)); }
QString rightBadge() { return QString(QChar(0x25b6)); }

OskKey makeKey(int row, int column, int span, const QString& label, OskAction action, const QString& face,
               const QString& faceTexture, const QString& badge, const QString& badgeTexture, bool blank,
               const QString& icon = QString()) {
    OskKey key;
    key.row = row;
    key.column = column;
    key.span = span;
    key.label = label;
    key.icon = icon;
    key.action = action;
    key.face = face;
    key.faceTexture = faceTexture;
    key.badge = badge;
    key.badgeTexture = badgeTexture;
    key.blank = blank;
    return key;
}

OskRow functionRow(int row, const OskCaptions& captions) {
    OskRow result;
    result.row = row;
    if (row == 4) {
        result.keys.push_back(makeKey(row, 0, 1, QStringLiteral("@#:"), OskAction::Palette, QStringLiteral("olive"), QStringLiteral("BUTTON_BASE_B_S"), QString(), QString(), false));
        result.keys.push_back(makeKey(row, 1, 1, QString(), OskAction::None, QStringLiteral("olive"), QStringLiteral("BUTTON_BASE_B_S"), QString(), QString(), true));
        result.keys.push_back(makeKey(row, 2, 1, accentLabel(), OskAction::Accent, QStringLiteral("olive"), QStringLiteral("BUTTON_BASE_B_S"), QStringLiteral("L2"), QStringLiteral("LABEL_L2"), false));
        result.keys.push_back(makeKey(row, 3, 1, QStringLiteral("a/A"), OskAction::Shift, QStringLiteral("olive"), QStringLiteral("BUTTON_BASE_B_S"), QStringLiteral("R2"), QStringLiteral("LABEL_R2"), false));
        result.keys.push_back(makeKey(row, 4, 2, captions.backspace, OskAction::Backspace, QStringLiteral("blue"), QStringLiteral("BUTTON_BASE_WHITE"), squareBadge(), QStringLiteral("LABEL_SQUARE"), false));
        result.keys.push_back(makeKey(row, 6, 2, captions.space, OskAction::Space, QStringLiteral("blue"), QStringLiteral("BUTTON_BASE_WHITE"), triangleBadge(), QStringLiteral("LABEL_TRIANGLE"), false));
        result.keys.push_back(makeKey(row, 8, 2, QString(), OskAction::None, QStringLiteral("blue"), QStringLiteral("BUTTON_BASE_WHITE"), QString(), QString(), true));
    } else if (row == 5) {
        result.keys.push_back(makeKey(row, 0, 1, upBadge(), OskAction::MoveUp, QStringLiteral("arrow"), QStringLiteral("LABEL_UP"), QString(), QString(), false));
        result.keys.push_back(makeKey(row, 1, 1, downBadge(), OskAction::MoveDown, QStringLiteral("arrow"), QStringLiteral("LABEL_DOWN"), QString(), QString(), false));
        result.keys.push_back(makeKey(row, 2, 1, leftBadge(), OskAction::CaretLeft, QStringLiteral("arrow"), QStringLiteral("LABEL_LEFT"), QStringLiteral("L1"), QStringLiteral("LABEL_L1"), false));
        result.keys.push_back(makeKey(row, 3, 1, rightBadge(), OskAction::CaretRight, QStringLiteral("arrow"), QStringLiteral("LABEL_RIGHT"), QStringLiteral("R1"), QStringLiteral("LABEL_R1"), false));
        result.keys.push_back(makeKey(row, 4, 1, QString(), OskAction::Palette, QStringLiteral("blue"), QStringLiteral("KEYTOP_COPY"), QString(), QString(), false, QStringLiteral("grid")));
        result.keys.push_back(makeKey(row, 5, 1, QString(), OskAction::None, QStringLiteral("mint"), QStringLiteral("BUTTON_BASE_SHORT"), QString(), QString(), true));
        result.keys.push_back(makeKey(row, 6, 2, captions.inputMode, OskAction::Mode, QStringLiteral("mint"), QStringLiteral("BUTTON_BASE_WHITE"), QStringLiteral("SELECT"), QStringLiteral("LABEL_SELECT"), false));
        result.keys.push_back(makeKey(row, 8, 2, captions.enter, OskAction::Enter, QStringLiteral("mint"), QStringLiteral("BUTTON_BASE_WHITE"), QStringLiteral("START"), QStringLiteral("LABEL_START"), false));
    }
    return result;
}

OskPage makeCharacterPage(const QString& name, const QStringList& rowStrings, const QString& modeCaption,
                          const OskCaptions& captions, const QString& provenance) {
    OskPage page;
    page.name = name;
    page.modeCaption = modeCaption;
    page.provenance = provenance;
    for (int row = 0; row < 4; ++row) {
        OskRow entry;
        entry.row = row;
        const QString text = row < rowStrings.size() ? rowStrings.at(row) : QString();
        for (int column = 0; column < 10; ++column) {
            const QChar c = column < text.size() ? text.at(column) : QChar();
            const bool blank = c.isNull();
            entry.keys.push_back(makeKey(row, column, 1, blank ? QString() : QString(c), blank ? OskAction::None : OskAction::Character,
                                         QStringLiteral("letter"), QString(), QString(), QString(), blank));
        }
        page.rows.push_back(entry);
    }
    page.rows.push_back(functionRow(4, captions));
    page.rows.push_back(functionRow(5, captions));
    return page;
}

OskPalette committedPalette() {
    OskPalette palette;
    palette.frame = QRect(595, 398, 353, 332);
    palette.gridOrigin = QPoint(634, 434);
    palette.cell = QSize(53, 53);
    palette.pitch = QSize(48, 48);
    palette.columns = 7;
    palette.rows = 7;
    palette.visibleRows = 5;
    palette.buttonCount = 49;
    palette.pageFlag = QChar(':');
    const QString symbols = QStringLiteral("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
    palette.symbols.reserve(symbols.size());
    for (const QChar c : symbols) palette.symbols.push_back(c);
    palette.provenance = QStringLiteral("committed table: module string at 0x40c7a without its one-character page flag");
    return palette;
}

struct TextureDecl {
    const char* name;
    const char* role;
    int width;
    int height;
    bool defaultSurface;
};

const TextureDecl kTextures[] = {
    {"BUTTON_BASE_WHITE", "keyFace2", 144, 54, true},
    {"BUTTON_BASE_B", "keyFace2Variant", 144, 54, true},
    {"BUTTON_BASE_G", "keyFace2Variant", 144, 54, true},
    {"BUTTON_BASE_O", "keyFace1", 72, 54, true},
    {"BUTTON_BASE_E", "keyFace1Variant", 72, 54, true},
    {"BUTTON_BASE_P", "keyFace1Variant", 72, 54, true},
    {"BUTTON_BASE_B_S", "keyFace1Short", 72, 54, true},
    {"BUTTON_BASE_SHORT", "keyFace1Mint", 72, 54, true},
    {"BUTTON_BASE_BLUE_GRAY", "keyFace2BlueGray", 144, 54, true},
    {"SBTN_FOCUS", "focus1", 84, 66, true},
    {"LBTN_FOCUS", "focusWide", 166, 66, true},
    {"BUTTON_FORCUS", "focusMenuDefault", 198, 36, true},
    {"LABEL_UP", "badgeUp", 72, 54, true},
    {"LABEL_DOWN", "badgeDown", 72, 54, true},
    {"LABEL_LEFT", "badgeLeft", 72, 54, true},
    {"LABEL_RIGHT", "badgeRight", 72, 54, true},
    {"LABEL_L1", "badgeL1", 74, 28, true},
    {"LABEL_R1", "badgeR1", 74, 28, true},
    {"LABEL_L2", "badgeL2", 74, 28, true},
    {"LABEL_R2", "badgeR2", 74, 28, true},
    {"LABEL_SQUARE", "badgeSquare", 74, 28, true},
    {"LABEL_TRIANGLE", "badgeTriangle", 74, 28, true},
    {"LABEL_SELECT", "badgeSelect", 74, 28, true},
    {"LABEL_START", "badgeStart", 74, 28, true},
    {"EN_LABEL_DELETE", "captionDelete", 140, 44, true},
    {"EN_LABEL_SPACE", "captionSpace", 140, 44, true},
    {"EN_LABEL_RETURN", "captionReturn", 140, 44, true},
    {"EN_LABEL_MODE", "captionMode", 140, 44, true},
    {"EN_LABEL_CLOSE", "captionClose", 140, 44, true},
    {"KEYTOP_COPY", "keytopCopy", 72, 54, true},
    {"KEYTOP_PASTE", "keytopPaste", 72, 54, true},
    {"MODE_BASE", "modeBase", 62, 30, true},
    {"KB_MODE_BASE", "keyboardModeBase", 62, 30, true},
    {"MODE_GUIDE_ALPHABET", "modeGlyphLower", 29, 30, true},
    {"MODE_GUIDE_ALPHABET_CAP", "modeGlyphCaps", 29, 30, true},
    {"CANDIDATE_WINDOW0x0", "candidateCorner", 10, 9, true},
    {"CANDIDATE_WINDOW1x1", "candidateCentre", 1, 1, true},
    {"CANDIDATE_WINDOW2x2", "candidateCorner2", 19, 18, true},
    {"SCROLL_BASE", "scrollBase", 12, 10, true},
    {"SCROLL_KNOB", "scrollKnob", 12, 10, true},
    {"LIST_WINDOW_FOCUS", "listFocus", 42, 42, true},
    {"tbase1x0", "panelSliceSample", 10, 15, true},
    {"tbase2x1", "panelSliceSample", 1, 328, true},
    {"tbase3x2", "panelSliceSample", 20, 62, true},
    {"BASE_SYMBOL", "paletteFrame", 60, 72, true},
    {"FOCUS_SYMBOL", "paletteFocus", 53, 53, true},
    {"SCROLL_BASE_SYMBOL", "paletteScrollBase", 12, 10, true},
    {"SCROLL_KNOB_SYMBOL", "paletteScrollKnob", 12, 10, true},
    {"BASE_NOTIFY", "notifyBase", 66, 66, false},
    {"MENU_BASE_0x0", "menuBaseSample", 20, 20, false},
    {"MENU_FOCUS", "menuFocus", 15, 15, false},
    {"POINTER", "pointer", 40, 47, false},
};

QVector<OskTexture> committedTextures() {
    QVector<OskTexture> textures;
    textures.reserve(int(sizeof(kTextures) / sizeof(kTextures[0])));
    for (const TextureDecl& decl : kTextures) {
        OskTexture texture;
        texture.name = QString::fromLatin1(decl.name);
        texture.role = QString::fromLatin1(decl.role);
        texture.size = QSize(decl.width, decl.height);
        texture.present = false;
        texture.defaultSurface = decl.defaultSurface;
        textures.push_back(texture);
    }
    return textures;
}

void addSlice(QHash<QString, QString>& slices, QStringList& ordered, const QString& group,
              int row, int column, const QString& image) {
    if (image.isEmpty()) return;
    slices.insert(group + QStringLiteral("/") + QString::number(row) + QStringLiteral("x") + QString::number(column), image);
    ordered.push_back(image);
}

OskLayout committedLayout() {
    OskLayout layout;
    layout.loaded = true;
    layout.firmware = false;
    layout.source = OskLayoutSource::Committed;
    layout.bands.panel = QRect(493, 290, 934, 500);
    layout.bands.titleBar = QRect(494, 290, 932, 46);
    layout.bands.titleIcon = QRect(506, 300, 12, 18);
    layout.bands.titleText = QRect(520, 300, 800, 20);
    layout.bands.field = QRect(494, 336, 932, 58);
    layout.bands.footer = QRect(493, 731, 934, 58);
    layout.bands.candidateList = QRect(1216, 394, 203, 246);
    layout.bands.predictionHeader = QRect(1216, 641, 203, 33);
    layout.bands.modeCell = QRect(1364, 641, 27, 33);
    layout.bands.countCell = QRect(1393, 641, 27, 33);
    layout.bands.copyButton = QRect(1217, 678, 62, 52);
    layout.bands.pasteButton = QRect(1322, 678, 62, 52);
    OskPredictionButton copy;
    copy.name = QStringLiteral("copy");
    copy.rect = layout.bands.copyButton;
    copy.action = OskAction::Copy;
    copy.texture = QStringLiteral("KEYTOP_COPY");
    copy.enabled = true;
    OskPredictionButton paste;
    paste.name = QStringLiteral("paste");
    paste.rect = layout.bands.pasteButton;
    paste.action = OskAction::Paste;
    paste.texture = QStringLiteral("KEYTOP_PASTE");
    paste.enabled = false;   // the captured keyboard has nothing on the clipboard
    layout.predictionButtons = QVector<OskPredictionButton>{copy, paste};
    layout.gridOrigin = QPoint(496, 406);
    layout.cell = QSize(72, 54);
    layout.face = QSize(66, 50);
    layout.faceInset = QPoint(3, 2);
    layout.columns = 10;
    layout.rows = 6;
    layout.keySeam = 6;
    layout.keyCornerRadius = 4;
    layout.defaultFocusRow = 2;
    layout.defaultFocusColumn = 4;   // 0-based; the capture/audit call this column 5 counting from 1
    layout.anchors.insert(QStringLiteral("focus.default"), QStringLiteral("row 2, column 4 (1-based column 5) = g"));

    layout.captions.space = QStringLiteral("Space");
    layout.captions.backspace = QStringLiteral("Backspace");
    layout.captions.inputMode = QStringLiteral("Input Mode");
    layout.captions.enter = QStringLiteral("Enter");
    layout.captions.prediction = QStringLiteral("Prediction");
    layout.captions.modeLower = QStringLiteral("a");
    layout.captions.modeCaps = QStringLiteral("A");
    layout.captions.source = QStringLiteral("committed table (work/osk-layout.proposed.json)");
    layout.captions.complete = true;

    layout.textStyle.present = true;
    layout.textStyle.size = 10;
    layout.textStyle.top = QColor(255, 255, 255);
    layout.textStyle.bottom = QColor(255, 255, 255);
    layout.textStyle.shadow = QColor(64, 64, 64, 255);
    layout.textStyle.shadowX = 4;
    layout.textStyle.shadowY = -5;
    layout.textStyle.shadowAlpha = 1;
    layout.textStyle.source = QStringLiteral("committed table (RCO fullkey_list_text: size 10, white, shadow 4/-5 at 0.25)");

    layout.caret = OskCaret{};
    layout.mask.glyph = QChar(0x2022);
    layout.mask.oneBulletPerCharacter = true;
    layout.mask.caretAfterLastBullet = true;
    layout.mask.source = QStringLiteral("shape only; metrics unverified (audit section 5.4)");

    layout.palette = committedPalette();
    const QStringList lower{QStringLiteral("1234567890"), QStringLiteral("qwertyuiop"),
                            QStringLiteral("asdfghjkl'"), QStringLiteral("zxcvbnm,.?")};
    const QStringList caps{QStringLiteral("!@#$%^&*()"), QStringLiteral("QWERTYUIOP"),
                           QStringLiteral("ASDFGHJKL\""), QStringLiteral("ZXCVBNM-_/")};
    layout.pages.push_back(makeCharacterPage(QStringLiteral("lower"), lower, layout.captions.modeLower, layout.captions,
                                             QStringLiteral("committed table: row strings and function rows (audit 4.2)")));
    layout.pages.push_back(makeCharacterPage(QStringLiteral("caps"), caps, layout.captions.modeCaps, layout.captions,
                                             QStringLiteral("committed table: row strings and function rows (audit 4.2)")));
    layout.pageNames = QStringList{QStringLiteral("lower"), QStringLiteral("caps"), QStringLiteral("symbolPalette")};

    layout.textures = committedTextures();
    layout.auxiliaryTextures = QStringList{QStringLiteral("MENU_BASE_0x0"), QStringLiteral("MENU_FOCUS"),
                                           QStringLiteral("BASE_NOTIFY"), QStringLiteral("POINTER")};
    layout.defaultSurfaceTextureCount = layout.textures.size() - layout.auxiliaryTextures.size();

    for (int row = 1; row <= 3; ++row) {
        for (int column = 0; column <= 3; ++column) {
            if (row == 1 && column == 3) continue;
            if (row == 3 && column == 3) continue;
            const QString image = QStringLiteral("tbase%1x%2").arg(row).arg(column);
            addSlice(layout.slices, layout.panelSlices, QStringLiteral("panel"), row, column, image);
        }
    }
    for (int row = 0; row <= 2; ++row) {
        for (int column = 0; column <= 2; ++column) {
            addSlice(layout.slices, layout.candidateSlices, QStringLiteral("candidate"), row, column,
                     QStringLiteral("CANDIDATE_WINDOW%1x%2").arg(row).arg(column));
            addSlice(layout.slices, layout.menuSlices, QStringLiteral("menu"), row, column,
                     QStringLiteral("MENU_BASE_%1x%2").arg(row).arg(column));
        }
    }
    layout.buttonBase1Col = QSize(72, 54);
    layout.buttonBase2Col = QSize(144, 54);
    layout.pointerSize = QSize(40, 47);
    layout.keyCount = 55;
    layout.characterKeyCount = 40;
    layout.row4KeyCount = 7;
    layout.row5KeyCount = 8;
    return layout;
}

void recountKeys(OskLayout& layout) {
    layout.keyCount = 0;
    layout.characterKeyCount = 0;
    layout.row4KeyCount = 0;
    layout.row5KeyCount = 0;
    // The counts describe one page (the default lower page), not both alternative pages.
    if (layout.pages.isEmpty()) return;
    for (const OskRow& row : layout.pages.first().rows) {
        layout.keyCount += row.keys.size();
        if (row.row < 4) layout.characterKeyCount += row.keys.size();
        if (row.row == 4) layout.row4KeyCount += row.keys.size();
        if (row.row == 5) layout.row5KeyCount += row.keys.size();
    }
}

void traceLayout(const OskLayout& layout) {
    qInfo().noquote() << layout.describe();
    int pass = 0, fail = 0, skip = 0;
    for (const OskAssertion& assertion : layout.assertions()) {
        if (!assertion.applicable) {
            ++skip;
            qInfo().noquote() << "osk: assertion" << assertion.name << "SKIP" << assertion.detail;
        } else if (assertion.ok) {
            ++pass;
            qInfo().noquote() << "osk: assertion" << assertion.name << "PASS" << assertion.detail;
        } else {
            ++fail;
            qWarning().noquote() << "osk: assertion" << assertion.name << "FAIL" << assertion.detail;
        }
    }
    qInfo().noquote() << QStringLiteral("osk: assertions %1 PASS, %2 FAIL, %3 SKIP").arg(pass).arg(fail).arg(skip);
}

QString shortSha(const QString& sha) {
    return sha.size() > 16 ? sha.left(16) + QStringLiteral("...") : sha;
}

} // namespace

QString oskActionName(OskAction action) {
    switch (action) {
    case OskAction::None: return QStringLiteral("none");
    case OskAction::Character: return QStringLiteral("character");
    case OskAction::Backspace: return QStringLiteral("backspace");
    case OskAction::Space: return QStringLiteral("space");
    case OskAction::Mode: return QStringLiteral("mode");
    case OskAction::Shift: return QStringLiteral("shift");
    case OskAction::Palette: return QStringLiteral("palette");
    case OskAction::Enter: return QStringLiteral("enter");
    case OskAction::Accent: return QStringLiteral("accent");
    case OskAction::MoveUp: return QStringLiteral("moveUp");
    case OskAction::MoveDown: return QStringLiteral("moveDown");
    case OskAction::CaretLeft: return QStringLiteral("caretLeft");
    case OskAction::CaretRight: return QStringLiteral("caretRight");
    case OskAction::Copy: return QStringLiteral("copy");
    case OskAction::Paste: return QStringLiteral("paste");
    }
    return QStringLiteral("unknown");
}

const OskKey* OskRow::keyAt(int column) const {
    for (const OskKey& key : keys) {
        if (column >= key.column && column < key.column + key.span) return &key;
    }
    return nullptr;
}

const OskRow* OskPage::rowAt(int row) const {
    for (const OskRow& entry : rows) {
        if (entry.row == row) return &entry;
    }
    return nullptr;
}

const OskKey* OskPage::keyAt(int row, int column) const {
    const OskRow* entry = rowAt(row);
    return entry ? entry->keyAt(column) : nullptr;
}

QChar OskPalette::symbolAt(int index) const {
    if (index < 0 || index >= symbols.size()) return QChar();
    return symbols.at(index);
}

QChar OskPalette::symbolAtCell(int row, int column, int scrollRow) const {
    if (row < 0 || row >= visibleRows || column < 0 || column >= columns) return QChar();
    return symbolAt((scrollRow + row) * columns + column);
}

QRect OskPalette::cellRect(int row, int column) const {
    if (row < 0 || row >= rows || column < 0 || column >= columns) return QRect();
    return QRect(gridOrigin.x() + column * pitch.width(), gridOrigin.y() + row * pitch.height(), cell.width(), cell.height());
}

const OskPage* OskLayout::page(int index) const {
    if (index < 0 || index >= pages.size()) return nullptr;
    return &pages.at(index);
}

const OskPage* OskLayout::page(const QString& name) const {
    for (const OskPage& entry : pages) {
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0) return &entry;
    }
    return nullptr;
}

const OskKey* OskLayout::keyAt(int pageIndex, int row, int column) const {
    const OskPage* entry = page(pageIndex);
    return entry ? entry->keyAt(row, column) : nullptr;
}

QRect OskLayout::cellRect(int pageIndex, int row, int column) const {
    if (pageIndex < 0 || pageIndex >= pages.size() || row < 0 || row >= rows || column < 0 || column >= columns)
        return QRect();
    return QRect(gridOrigin.x() + column * cell.width(), gridOrigin.y() + row * cell.height(), cell.width(), cell.height());
}

QRect OskLayout::keyFaceRect(int pageIndex, int row, int column) const {
    const OskKey* key = keyAt(pageIndex, row, column);
    if (!key) return QRect();
    const int x = gridOrigin.x() + key->column * cell.width() + faceInset.x();
    const int y = gridOrigin.y() + row * cell.height() + faceInset.y();
    const int w = key->span * cell.width() - 2 * faceInset.x();
    const int h = cell.height() - 2 * faceInset.y();
    return QRect(x, y, w, h);
}

QRect OskLayout::paletteCellRect(int row, int column) const {
    return palette.cellRect(row, column);
}

QString OskLayout::describe() const {
    QStringList lines;
    lines << QStringLiteral("osk: resolved layout (%1)").arg(source == OskLayoutSource::Firmware ? QStringLiteral("firmware") : QStringLiteral("committed fallback"));
    lines << QStringLiteral("  panel            %1,%2 %3x%4").arg(bands.panel.x()).arg(bands.panel.y()).arg(bands.panel.width()).arg(bands.panel.height());
    lines << QStringLiteral("  title/field/footer %1x%2 + %3x%4 + %5x%6")
                 .arg(bands.titleBar.width()).arg(bands.titleBar.height())
                 .arg(bands.field.width()).arg(bands.field.height())
                 .arg(bands.footer.width()).arg(bands.footer.height());
    lines << QStringLiteral("  grid             %1,%2 cell %3x%4 face %5x%6 %7x%8")
                 .arg(gridOrigin.x()).arg(gridOrigin.y())
                 .arg(cell.width()).arg(cell.height())
                 .arg(face.width()).arg(face.height())
                 .arg(columns).arg(rows);
    lines << QStringLiteral("  keys             rows 0-3: 10+10+10+10; row 4: %1; row 5: %2; total %3 (%4 character)")
                 .arg(row4KeyCount).arg(row5KeyCount).arg(keyCount).arg(characterKeyCount);
    lines << QStringLiteral("  pages            %1").arg(pageNames.join(QStringLiteral(", ")));
    lines << QStringLiteral("  palette          %1x%2 (%3 buttons, %4 visible rows), %5 symbols, origin %6,%7 cell %8x%9 pitch %10x%11 frame %12,%13 %14x%15")
                 .arg(palette.columns).arg(palette.rows).arg(palette.buttonCount).arg(palette.visibleRows)
                 .arg(int(palette.symbols.size()))
                 .arg(palette.gridOrigin.x()).arg(palette.gridOrigin.y())
                 .arg(palette.cell.width()).arg(palette.cell.height())
                 .arg(palette.pitch.width()).arg(palette.pitch.height())
                 .arg(palette.frame.x()).arg(palette.frame.y()).arg(palette.frame.width()).arg(palette.frame.height());
    lines << QStringLiteral("  prediction       list %1,%2 %3x%4; header %5,%6 %7x%8; copy %9,%10 %11x%12; paste %13,%14 %15x%16")
                 .arg(bands.candidateList.x()).arg(bands.candidateList.y()).arg(bands.candidateList.width()).arg(bands.candidateList.height())
                 .arg(bands.predictionHeader.x()).arg(bands.predictionHeader.y()).arg(bands.predictionHeader.width()).arg(bands.predictionHeader.height())
                 .arg(bands.copyButton.x()).arg(bands.copyButton.y()).arg(bands.copyButton.width()).arg(bands.copyButton.height())
                 .arg(bands.pasteButton.x()).arg(bands.pasteButton.y()).arg(bands.pasteButton.width()).arg(bands.pasteButton.height());
    lines << QStringLiteral("  captions         %1 | %2 | %3 | %4 | %5 (%6)")
                 .arg(captions.space).arg(captions.backspace).arg(captions.inputMode).arg(captions.enter)
                 .arg(captions.prediction).arg(captions.source);
    lines << QStringLiteral("  caret            %1,%2,%3 height %4 shaft %5 serif %6 blink %7 ms (%8)")
                 .arg(caret.colour.red()).arg(caret.colour.green()).arg(caret.colour.blue())
                 .arg(caret.height).arg(caret.shaftWidth).arg(caret.serifWidth).arg(caret.blinkMilliseconds)
                 .arg(caret.blinkTraced ? QStringLiteral("traced") : QStringLiteral("traced from the caret burst"));
    lines << QStringLiteral("  dim              %1,%2,%3 alpha %4")
                 .arg(bands.dim.red()).arg(bands.dim.green()).arg(bands.dim.blue()).arg(bands.dimAlpha, 0, 'f', 2);
    lines << QStringLiteral("  focus            row %1, column %2 (1-based column %3) = %4")
                 .arg(defaultFocusRow).arg(defaultFocusColumn).arg(defaultFocusColumn + 1)
                 .arg(keyAt(0, defaultFocusRow, defaultFocusColumn) ? keyAt(0, defaultFocusRow, defaultFocusColumn)->label : QStringLiteral("?"));
    lines << QStringLiteral("  textures         %1 named, %2 default-surface, %3 in image table, %4 decoded")
                 .arg(int(textures.size())).arg(defaultSurfaceTextureCount).arg(cost.texturesResolved).arg(cost.texturesDecoded);
    lines << QStringLiteral("  provenance       %1").arg(provenance);
    lines << QStringLiteral("  cost             total %1 ms (rco %2, text %3, module %4, textures %5); rco %6 B, module %7 B, textures %8/%9")
                 .arg(cost.totalMs(), 0, 'f', 2)
                 .arg(double(cost.rcoNs) / 1.0e6, 0, 'f', 2)
                 .arg(double(cost.textNs) / 1.0e6, 0, 'f', 2)
                 .arg(double(cost.moduleNs) / 1.0e6, 0, 'f', 2)
                 .arg(double(cost.textureNs) / 1.0e6, 0, 'f', 2)
                 .arg(cost.rcoBytes).arg(cost.moduleBytes)
                 .arg(cost.texturesResolved).arg(int(textures.size()));
    for (const QString& line : provenanceChain) lines << QStringLiteral("  source           %1").arg(line);
    return lines.join(QChar('\n'));
}

QVector<OskAssertion> OskLayout::assertions() const {
    QVector<OskAssertion> result;
    const auto add = [&](const QString& name, bool ok, const QString& detail, bool applicable = true) {
        OskAssertion assertion;
        assertion.name = name;
        assertion.ok = ok;
        assertion.applicable = applicable;
        assertion.detail = detail;
        result.push_back(assertion);
    };
    add(QStringLiteral("loaded"), loaded, QStringLiteral("layout object populated"));
    if (!loaded) return result;
    add(QStringLiteral("panel"), bands.panel == QRect(493, 290, 934, 500),
        QStringLiteral("capture 493,290 934x500; got %1,%2 %3x%4").arg(bands.panel.x()).arg(bands.panel.y()).arg(bands.panel.width()).arg(bands.panel.height()));
    add(QStringLiteral("titleBar"), bands.titleBar == QRect(494, 290, 932, 46), QStringLiteral("%1,%2 %3x%4").arg(bands.titleBar.x()).arg(bands.titleBar.y()).arg(bands.titleBar.width()).arg(bands.titleBar.height()));
    add(QStringLiteral("field"), bands.field == QRect(494, 336, 932, 58), QStringLiteral("%1,%2 %3x%4").arg(bands.field.x()).arg(bands.field.y()).arg(bands.field.width()).arg(bands.field.height()));
    add(QStringLiteral("footer"), bands.footer == QRect(493, 731, 934, 58), QStringLiteral("%1,%2 %3x%4").arg(bands.footer.x()).arg(bands.footer.y()).arg(bands.footer.width()).arg(bands.footer.height()));
    add(QStringLiteral("gridOrigin"), gridOrigin == QPoint(496, 406), QStringLiteral("%1,%2").arg(gridOrigin.x()).arg(gridOrigin.y()));
    add(QStringLiteral("cell"), cell == QSize(72, 54), QStringLiteral("%1x%2").arg(cell.width()).arg(cell.height()));
    add(QStringLiteral("face"), face == QSize(66, 50), QStringLiteral("%1x%2").arg(face.width()).arg(face.height()));
    add(QStringLiteral("columns"), columns == 10, QString::number(columns));
    add(QStringLiteral("rows"), rows == 6, QString::number(rows));
    add(QStringLiteral("pages"), pages.size() == 2 && pageNames.size() == 3,
        QStringLiteral("%1 key pages, page set %2").arg(int(pages.size())).arg(pageNames.join(QStringLiteral(", "))));

    const auto rowKeys = [&](int pageIndex, int row) {
        const OskPage* entry = page(pageIndex);
        const OskRow* line = entry ? entry->rowAt(row) : nullptr;
        return line ? line->keys.size() : 0;
    };
    add(QStringLiteral("rows0-3"), rowKeys(0, 0) == 10 && rowKeys(0, 1) == 10 && rowKeys(0, 2) == 10 && rowKeys(0, 3) == 10,
        QStringLiteral("10+10+10+10 keys"));
    add(QStringLiteral("capsKeyGrid"), int(rowKeys(1, 0)) == 10 && int(rowKeys(1, 1)) == 10 && int(rowKeys(1, 2)) == 10 &&
                                           int(rowKeys(1, 3)) == 10 && int(rowKeys(1, 4)) == 7 && int(rowKeys(1, 5)) == 8,
        QStringLiteral("caps page 10+10+10+10+7+8"));
    add(QStringLiteral("row4"), int(rowKeys(0, 4)) == 7, QStringLiteral("%1 objects").arg(int(rowKeys(0, 4))));
    add(QStringLiteral("row5"), int(rowKeys(0, 5)) == 8, QStringLiteral("%1 objects").arg(int(rowKeys(0, 5))));
    add(QStringLiteral("keyObjects"), keyCount == 55 && characterKeyCount == 40 && row4KeyCount == 7 && row5KeyCount == 8,
        QStringLiteral("55 objects = 40 character + 7 row 4 + 8 row 5; the brief's 48 matches the default-surface texture subset"));
    const auto rowText = [&](int pageIndex, int row) {
        QString text;
        const OskPage* entry = page(pageIndex);
        const OskRow* line = entry ? entry->rowAt(row) : nullptr;
        if (!line) return text;
        for (const OskKey& key : line->keys) {
            if (key.action == OskAction::Character) text += key.label;
        }
        return text;
    };
    const bool lowerRows = rowText(0, 0) == QStringLiteral("1234567890") && rowText(0, 1) == QStringLiteral("qwertyuiop") &&
                           rowText(0, 2) == QStringLiteral("asdfghjkl'") && rowText(0, 3) == QStringLiteral("zxcvbnm,.?");
    const bool capsRows = rowText(1, 0) == QStringLiteral("!@#$%^&*()") && rowText(1, 1) == QStringLiteral("QWERTYUIOP") &&
                          rowText(1, 2) == QStringLiteral("ASDFGHJKL\"") && rowText(1, 3) == QStringLiteral("ZXCVBNM-_/");
    add(QStringLiteral("lowerRows"), lowerRows, QStringLiteral("1234567890 / qwertyuiop / asdfghjkl' / zxcvbnm,.?"));
    add(QStringLiteral("capsRows"), capsRows, QStringLiteral("!@#$%^&*() / QWERTYUIOP / ASDFGHJKL\" / ZXCVBNM-_/"));
    add(QStringLiteral("moduleRows"), moduleRows, QStringLiteral("character rows read from the module label table"), firmware);
    add(QStringLiteral("paletteGrid"), palette.columns == 7 && palette.rows == 7 && palette.visibleRows == 5 && palette.buttonCount == 49,
        QStringLiteral("%1x%2, %3 buttons, %4 visible").arg(palette.columns).arg(palette.rows).arg(palette.buttonCount).arg(palette.visibleRows));
    add(QStringLiteral("paletteSymbols"), palette.symbols.size() == 32 && palette.symbolAt(0) == QChar('!') && palette.symbolAt(31) == QChar('~'),
        QStringLiteral("%1 symbols, first '%2', last '%3', flag '%4'").arg(int(palette.symbols.size())).arg(palette.symbolAt(0)).arg(palette.symbolAt(31)).arg(palette.pageFlag));
    add(QStringLiteral("textures"), textures.size() == 52 && defaultSurfaceTextureCount == 48,
        QStringLiteral("52 named; 48 default-surface; 4 auxiliary (%1)").arg(auxiliaryTextures.join(QStringLiteral(", "))));
    add(QStringLiteral("texturesResolved"), texturesResolved && cost.texturesResolved == 52,
        QStringLiteral("%1/52 names in the RCO image table, %2 decoded through Assets::image").arg(cost.texturesResolved).arg(cost.texturesDecoded), firmware);
    // The 14 GIM pixel-format 0x5 pictures turned out to be palette-less 8-bit alpha masks (they carry
    // no type-0x05 palette block anywhere in dev_flash), and Assets::decodeGim now decodes them as
    // white + coverage, so every named texture decodes: this is a real assertion, not an informational
    // skip, and it fails if a future container adds a format the decoder still cannot read.
    add(QStringLiteral("texturesDecoded"), cost.texturesDecoded == cost.texturesResolved && cost.texturesResolved > 0,
        firmware ? QStringLiteral("%1/%2 decoded (RGBA8888, DXT5 and the palette-less 8-bit coverage masks)")
                       .arg(cost.texturesDecoded).arg(cost.texturesResolved)
                 : QStringLiteral("not applicable (no dev-flash; no texture decode attempted)"),
        firmware);
    add(QStringLiteral("captions"), captions.space == QStringLiteral("Space") && captions.backspace == QStringLiteral("Backspace") &&
                                      captions.inputMode == QStringLiteral("Input Mode") && captions.enter == QStringLiteral("Enter") &&
                                      captions.prediction == QStringLiteral("Prediction") && captions.modeLower == QStringLiteral("a") &&
                                      captions.modeCaps == QStringLiteral("A"),
        QStringLiteral("Space / Backspace / Input Mode / Enter / Prediction, mode a/A"));
    add(QStringLiteral("captionSource"), captionsFromRco, QStringLiteral("RCO text table"), firmware);
    add(QStringLiteral("textStyle"), textStyle.present && qFuzzyCompare(textStyle.size, 10.0) && qFuzzyCompare(textStyle.shadowX, 4.0) && qFuzzyCompare(textStyle.shadowY, -5.0),
        QStringLiteral("size %1, shadow %2/%3 at alpha %4").arg(textStyle.size).arg(textStyle.shadowX).arg(textStyle.shadowY).arg(textStyle.shadowAlpha), firmware);
    add(QStringLiteral("caret"), caret.colour == QColor(130, 249, 12) && caret.height == 27 && caret.shaftWidth == 2 && caret.serifWidth == 12,
        QStringLiteral("%1,%2,%3 height %4 shaft %5 serif %6").arg(caret.colour.red()).arg(caret.colour.green()).arg(caret.colour.blue()).arg(caret.height).arg(caret.shaftWidth).arg(caret.serifWidth));
    add(QStringLiteral("dim"), qAbs(bands.dimAlpha - 0.70) < 1e-6, QStringLiteral("alpha %1").arg(bands.dimAlpha, 0, 'f', 2));
    add(QStringLiteral("defaultFocus"), defaultFocusRow == 2 && defaultFocusColumn == 4 &&
                                            keyAt(0, defaultFocusRow, defaultFocusColumn) &&
                                            keyAt(0, defaultFocusRow, defaultFocusColumn)->label == QStringLiteral("g"),
        QStringLiteral("row %1, column %2 (1-based %3) = g").arg(defaultFocusRow).arg(defaultFocusColumn).arg(defaultFocusColumn + 1));
    add(QStringLiteral("predictionRects"), bands.candidateList == QRect(1216, 394, 203, 246) &&
                                               bands.predictionHeader == QRect(1216, 641, 203, 33) &&
                                               bands.copyButton == QRect(1217, 678, 62, 52) &&
                                               bands.pasteButton == QRect(1322, 678, 62, 52),
        QStringLiteral("list, header, copy, paste at the capture positions"));
    add(QStringLiteral("predictionButtons"), predictionButtons.size() == 2 &&
                                                predictionButtons.at(0).action == OskAction::Copy &&
                                                predictionButtons.at(1).action == OskAction::Paste &&
                                                predictionButtons.at(0).enabled && !predictionButtons.at(1).enabled,
        QStringLiteral("copy enabled, paste disabled (capture state)"));
    add(QStringLiteral("rcoPaletteGrid"), anchors.value(QStringLiteral("symbol_palette_bg")).startsWith(QStringLiteral("7x7")),
        anchors.value(QStringLiteral("symbol_palette_bg"), QStringLiteral("not read")), firmware);
    add(QStringLiteral("rcoPaletteButtons"), anchors.value(QStringLiteral("symbol_palette_group.sym_btn")) == QStringLiteral("49"),
        anchors.value(QStringLiteral("symbol_palette_group.sym_btn"), QStringLiteral("not read")), firmware);
    return result;
}

bool OskLayout::passesAssertions() const {
    for (const OskAssertion& assertion : assertions()) {
        if (assertion.applicable && !assertion.ok) return false;
    }
    return true;
}

OskLayout loadOskLayout(Assets& assets, RcoObjects& objects) {
    QElapsedTimer total;
    total.start();
    const bool trace = qEnvironmentVariableIntValue("XMB_OSK_LAYOUT_LOG") == 1;
    OskLayout layout = committedLayout();
    layout.provenanceChain.clear();

    if (assets.root().isEmpty()) {
        layout.cost.totalNs = total.nsecsElapsed();
        layout.provenance = QStringLiteral("committed table (work/osk-layout.proposed.json); dev-flash absent");
        layout.provenanceChain
            << layout.provenance
            << QStringLiteral("palette: 32 symbols from the module string behind its one-character page flag (the proposed table's 94-printable-ASCII note is superseded)")
            << QStringLiteral("textures: 52 declared, 48 default-surface, 0 decoded (no dev-flash)");
        qInfo().noquote() << "osk: layout from committed table (provenance work/osk-layout.proposed.json; dev-flash absent)";
        if (trace) traceLayout(layout);
        return layout;
    }

    QElapsedTimer phase;
    phase.start();
    const RcoObjects::Container& container = objects.container(QStringLiteral("oskfullkeypanel_plugin"));
    layout.cost.rcoNs = phase.nsecsElapsed();
    if (!container.ok) {
        layout.cost.totalNs = total.nsecsElapsed();
        layout.provenance = QStringLiteral("committed table (work/osk-layout.proposed.json); oskfullkeypanel_plugin.rco unreadable: %1").arg(container.error);
        layout.provenanceChain << layout.provenance;
        qInfo().noquote() << "osk: layout from committed table (provenance work/osk-layout.proposed.json; oskfullkeypanel_plugin.rco unreadable:"
                          << container.error << ")";
        if (trace) traceLayout(layout);
        return layout;
    }

    layout.firmware = true;
    layout.source = OskLayoutSource::Firmware;
    layout.cost.rcoBytes = int(container.fileBytes);
    const int objectCount = container.objects.size();
    const int imageCount = container.images().size();

    phase.restart();
    const QVector<RcoObjects::TextRow> textRows = container.textRows();
    layout.cost.textNs = phase.nsecsElapsed();
    QHash<QString, QString> texts;
    texts.reserve(textRows.size());
    for (const RcoObjects::TextRow& row : textRows) {
        if (!row.label.isEmpty() && !texts.contains(row.label)) texts.insert(row.label, row.text);
    }
    const auto captionText = [&](const QString& label, const QString& fallback) {
        const auto it = texts.constFind(label);
        return it != texts.cend() ? it.value() : fallback;
    };
    const bool captionsPresent = texts.contains(QStringLiteral("msg_space_osk")) &&
                                 texts.contains(QStringLiteral("msg_back_space_osk")) &&
                                 texts.contains(QStringLiteral("GRAPHIC_input_mode")) &&
                                 texts.contains(QStringLiteral("GRAPHIC_enter")) &&
                                 texts.contains(QStringLiteral("msg_prediction"));
    layout.captions.space = captionText(QStringLiteral("msg_space_osk"), layout.captions.space);
    layout.captions.backspace = captionText(QStringLiteral("msg_back_space_osk"), layout.captions.backspace);
    layout.captions.inputMode = captionText(QStringLiteral("GRAPHIC_input_mode"), layout.captions.inputMode);
    layout.captions.enter = captionText(QStringLiteral("GRAPHIC_enter"), layout.captions.enter);
    layout.captions.prediction = captionText(QStringLiteral("msg_prediction"), layout.captions.prediction);
    const QString modeCaps = captionText(QStringLiteral("GRAPHIC_input_mode_en"), QStringLiteral("A"));
    layout.captions.modeCaps = modeCaps.isEmpty() ? QStringLiteral("A") : modeCaps.left(1);
    layout.captions.modeLower = layout.captions.modeCaps.toLower();
    layout.captions.source = captionsPresent
        ? QStringLiteral("RCO text table (oskfullkeypanel_plugin; msg_space_osk, msg_back_space_osk, GRAPHIC_input_mode, GRAPHIC_enter, msg_prediction)")
        : QStringLiteral("RCO text table, partial; committed strings fill the gaps");
    layout.captions.complete = captionsPresent;
    layout.captionsFromRco = captionsPresent;

    // RCO geometry anchors and image references.
    if (const RcoObjects::Object* object = container.object(QStringLiteral("fullkey_osk_init_point"))) {
        layout.anchors.insert(QStringLiteral("fullkey_osk_init_point"),
                              QStringLiteral("%1x%2 image %3").arg(container.number(*object, QStringLiteral("width"), 0))
                                  .arg(container.number(*object, QStringLiteral("height"), 0))
                                  .arg(imageRef(container, object)));
    }
    if (const RcoObjects::Object* object = container.object(QStringLiteral("fullkey_list_base")))
        layout.anchors.insert(QStringLiteral("fullkey_list_base"), imageRef(container, object).isEmpty() ? QStringLiteral("image null") : imageRef(container, object));
    if (const RcoObjects::Object* object = container.object(QStringLiteral("fullkey_osk_panel_base")))
        layout.anchors.insert(QStringLiteral("fullkey_osk_panel_base"), imageRef(container, object).isEmpty() ? QStringLiteral("image null") : imageRef(container, object));
    if (const RcoObjects::Object* object = container.object(QStringLiteral("symbol_palette_bg"))) {
        const int width = int(container.number(*object, QStringLiteral("width"), 0));
        const int height = int(container.number(*object, QStringLiteral("height"), 0));
        layout.anchors.insert(QStringLiteral("symbol_palette_bg"), QStringLiteral("%1x%2 image %3").arg(width).arg(height).arg(imageRef(container, object)));
        if (width > 0 && height > 0) {
            layout.palette.columns = width;
            layout.palette.rows = height;
        }
    }
    if (const RcoObjects::Object* group = container.object(QStringLiteral("symbol_palette_group"))) {
        int buttons = 0;
        for (const RcoObjects::Object* child : container.children(*group)) {
            if (child && child->name.startsWith(QStringLiteral("sym_btn"))) ++buttons;
        }
        layout.anchors.insert(QStringLiteral("symbol_palette_group.sym_btn"), QString::number(buttons));
        if (buttons > 0) layout.palette.buttonCount = buttons;
    }
    if (const RcoObjects::Object* pointer = container.object(QStringLiteral("move_pointer"))) {
        const int width = int(container.number(*pointer, QStringLiteral("width"), 0));
        const int height = int(container.number(*pointer, QStringLiteral("height"), 0));
        if (width > 0 && height > 0) layout.pointerSize = QSize(width, height);
        layout.anchors.insert(QStringLiteral("move_pointer"), QStringLiteral("%1x%2 image %3").arg(width).arg(height).arg(imageRef(container, pointer)));
    }
    const auto sliceObject = [&](const QString& name, const QString& key) {
        if (const RcoObjects::Object* object = container.object(name)) {
            const QString image = imageRef(container, object);
            if (!image.isEmpty()) layout.slices.insert(key, image);
        }
    };
    sliceObject(QStringLiteral("fullkey_button_input_mode"), QStringLiteral("mode/base"));
    sliceObject(QStringLiteral("fullkey_button_input_mode_kb"), QStringLiteral("mode/kb"));
    sliceObject(QStringLiteral("fullkey_button_list_focus"), QStringLiteral("focus/rcoDefault"));
    sliceObject(QStringLiteral("fullkey_list_window_focus"), QStringLiteral("list/focus"));
    sliceObject(QStringLiteral("fullkey_scroll_base"), QStringLiteral("scroll/base"));
    sliceObject(QStringLiteral("fullkey_scroll_knob"), QStringLiteral("scroll/knob"));
    sliceObject(QStringLiteral("fullkey_list_gem_guide_top"), QStringLiteral("guide/top"));
    sliceObject(QStringLiteral("fullkey_list_gem_guide_bottom"), QStringLiteral("guide/bottom"));

    const auto addSlices = [&](const QString& prefix, const QString& group, int sliceRows, int sliceColumns) {
        for (int row = 0; row < sliceRows; ++row) {
            for (int column = 0; column < sliceColumns; ++column) {
                const QString name = QStringLiteral("%1%2x%3").arg(prefix).arg(row).arg(column);
                if (const RcoObjects::Object* object = container.object(name)) {
                    const QString image = imageRef(container, object);
                    if (image.isEmpty()) continue;
                    if (group == QStringLiteral("panel")) addSlice(layout.slices, layout.panelSlices, group, row, column, image);
                    else if (group == QStringLiteral("candidate")) addSlice(layout.slices, layout.candidateSlices, group, row, column, image);
                    else if (group == QStringLiteral("menu")) addSlice(layout.slices, layout.menuSlices, group, row, column, image);
                    else addSlice(layout.slices, layout.menuSlices, group, row, column, image);
                }
            }
        }
    };
    addSlices(QStringLiteral("oskpanel_base"), QStringLiteral("panel"), 4, 4);
    addSlices(QStringLiteral("fullkey_candidate_list_base"), QStringLiteral("candidate"), 3, 3);
    addSlices(QStringLiteral("fullkey_menuelist_base"), QStringLiteral("menu"), 3, 3);

    // Text material: the RCO Text object's own fields, through Materials (not a new parser).
    Materials materials(assets, objects);
    const Materials::TextMaterial textMaterial = materials.textMaterial(QStringLiteral("oskfullkeypanel_plugin"), QStringLiteral("fullkey_list_text"));
    if (textMaterial.present) {
        layout.textStyle.present = true;
        layout.textStyle.size = textMaterial.size > 0 ? textMaterial.size : 10;
        layout.textStyle.top = textMaterial.top;
        layout.textStyle.bottom = textMaterial.bottom;
        layout.textStyle.shadow = textMaterial.shadow;
        layout.textStyle.shadowX = textMaterial.shadowX;
        layout.textStyle.shadowY = textMaterial.shadowY;
        layout.textStyle.shadowAlpha = textMaterial.shadow.alphaF();
        layout.textStyle.fontReference = textMaterial.fontReference;
        layout.textStyle.source = QStringLiteral("RCO Text object fullkey_list_text (Materials::textMaterial)");
        layout.anchors.insert(QStringLiteral("fullkey_list_text.size"), QString::number(layout.textStyle.size, 'g', 8));
    } else {
        layout.textStyle.source = QStringLiteral("committed table (the RCO Text object could not be resolved)");
    }

    // Texture name resolution: every declared name against the RCO image table, then the existing
    // Assets::image path for each one (the same call View::drawImage makes).
    phase.restart();
    const QHash<quint32, QString> imageTable = container.images();
    QSet<QString> imageNames;
    imageNames.reserve(imageTable.size());
    for (auto it = imageTable.cbegin(); it != imageTable.cend(); ++it) imageNames.insert(it.value());
    const QString containerName = QStringLiteral("oskfullkeypanel_plugin");
    int presentTextures = 0;
    int decodedTextures = 0;
    for (OskTexture& texture : layout.textures) {
        texture.present = imageNames.contains(texture.name);
        if (!texture.present) {
            qWarning().noquote() << "osk: texture" << texture.name << "is not in the RCO image table";
            continue;
        }
        ++presentTextures;
        const QImage& image = assets.image(containerName, texture.name);
        if (image.isNull()) {
            qWarning().noquote() << "osk: texture" << texture.name << "is named but the existing GIM decoder returned no pixels";
            continue;
        }
        texture.size = image.size();
        texture.decoded = true;
        ++decodedTextures;
    }
    layout.cost.textureNs = phase.nsecsElapsed();
    layout.cost.texturesResolved = presentTextures;
    layout.cost.texturesDecoded = decodedTextures;
    layout.texturesResolved = presentTextures == layout.textures.size();
    layout.texturesDecoded = decodedTextures == layout.textures.size();

    const QImage& oneColumn = assets.image(containerName, QStringLiteral("BUTTON_BASE_B_S"));
    const QImage& twoColumn = assets.image(containerName, QStringLiteral("BUTTON_BASE_WHITE"));
    if (!oneColumn.isNull()) layout.buttonBase1Col = oneColumn.size();
    if (!twoColumn.isNull()) layout.buttonBase2Col = twoColumn.size();
    if (layout.buttonBase1Col.width() > 0 && layout.buttonBase1Col.height() > 0) {
        layout.cell = layout.buttonBase1Col;
        layout.face = QSize(qMax(1, layout.cell.width() - layout.keySeam), qMax(1, layout.cell.height() - 4));
        layout.faceInset = QPoint(layout.keySeam / 2, 2);
    }

    // The auxiliary-page classification: names referenced only under fullkey_menue, notify_window,
    // copy_window or osk_pointer are the four that are not on the keyboard surface.
    QSet<QString> panelImages;
    QSet<QString> auxiliaryImages;
    for (const RcoObjects::Object& object : container.objects) {
        const QString image = imageRef(container, &object);
        if (image.isEmpty()) continue;
        const QString page = objectPage(container, object);
        if (page == QStringLiteral("fullkey_panel") || page == QStringLiteral("symbol_palette")) panelImages.insert(image);
        else if (page == QStringLiteral("fullkey_menue") || page == QStringLiteral("notify_window") ||
                 page == QStringLiteral("copy_window") || page == QStringLiteral("osk_pointer")) auxiliaryImages.insert(image);
    }
    QSet<QString> auxiliaryOnly = auxiliaryImages;
    auxiliaryOnly.subtract(panelImages);
    QSet<QString> declaredNames;
    for (const OskTexture& texture : layout.textures) declaredNames.insert(texture.name);
    QStringList classifiedAuxiliary;
    for (const QString& name : auxiliaryOnly) {
        if (declaredNames.contains(name)) classifiedAuxiliary.push_back(name);
    }
    classifiedAuxiliary.sort();
    QStringList declaredAuxiliary;
    for (const OskTexture& texture : layout.textures) {
        if (!texture.defaultSurface) declaredAuxiliary.push_back(texture.name);
    }
    declaredAuxiliary.sort();
    if (classifiedAuxiliary == declaredAuxiliary && !classifiedAuxiliary.isEmpty()) {
        layout.auxiliaryTextures = classifiedAuxiliary;
    } else {
        layout.auxiliaryTextures = declaredAuxiliary;
        layout.provenanceChain << QStringLiteral("textures: object-tree page classification incomplete (%1 auxiliary-only vs %2 declared); committed auxiliary set used")
                                      .arg(int(classifiedAuxiliary.size())).arg(int(declaredAuxiliary.size()));
    }
    layout.defaultSurfaceTextureCount = layout.textures.size() - layout.auxiliaryTextures.size();

    // The module: row strings, label table and palette string. The dev_flash .sprx is encrypted;
    // Assets::vshModule reads the decrypted .prx from --vsh-modules.
    phase.restart();
    const QByteArray module = assets.vshModule(QStringLiteral("oskfullkeypanel_plugin.prx"));
    layout.cost.moduleBytes = module.size();
    QString moduleSha;
    ModuleSegments segments;
    QString moduleError;
    bool moduleOk = false;
    if (module.isEmpty()) {
        moduleError = QStringLiteral("no decrypted oskfullkeypanel_plugin.prx (pass --vsh-modules)");
    } else if (!parseModule(module, segments)) {
        moduleError = segments.error;
    } else {
        moduleOk = true;
        moduleSha = QString::fromLatin1(QCryptographicHash::hash(module, QCryptographicHash::Sha256).toHex());
        layout.anchors.insert(QStringLiteral("module.sha256"), moduleSha);
        layout.anchors.insert(QStringLiteral("module.bytes"), QString::number(module.size()));
    }

    if (moduleOk) {
        const QVector<LabelGroup> groups = scanLabelGroups(module, segments);
        const LabelGroup* lower = nullptr;
        const LabelGroup* caps = nullptr;
        for (const LabelGroup& group : groups) {
            const bool lowerLike = group.rows.size() == 4 && group.rows.at(1) == QStringLiteral("qwertyuiop") &&
                                   group.rows.at(2).startsWith(QStringLiteral("asdfghjkl")) &&
                                   group.rows.at(3).startsWith(QStringLiteral("zxcvbnm"));
            if (!lower && lowerLike) lower = &group;
        }
        if (lower) {
            for (const LabelGroup& group : groups) {
                if (group.offset <= lower->offset) continue;
                const bool capsLike = group.rows.size() == 4 && group.rows.at(1) == QStringLiteral("QWERTYUIOP") &&
                                      group.rows.at(2).startsWith(QStringLiteral("ASDFGHJKL")) &&
                                      group.rows.at(3).startsWith(QStringLiteral("ZXCVBNM"));
                if (capsLike) {
                    caps = &group;
                    break;
                }
            }
            if (!caps) {
                for (const LabelGroup& group : groups) {
                    if (&group == lower) continue;
                    const bool capsLike = group.rows.size() == 4 && group.rows.at(1) == QStringLiteral("QWERTYUIOP") &&
                                          group.rows.at(2).startsWith(QStringLiteral("ASDFGHJKL"));
                    if (capsLike) {
                        caps = &group;
                        break;
                    }
                }
            }
        }
        if (lower && caps) {
            const QString lowerIds = QStringLiteral("0x%1..0x%2").arg(lower->ids.first(), 0, 16).arg(lower->ids.last(), 0, 16);
            const QString capsIds = QStringLiteral("0x%1..0x%2").arg(caps->ids.first(), 0, 16).arg(caps->ids.last(), 0, 16);
            layout.pages[0] = makeCharacterPage(QStringLiteral("lower"), lower->rows, layout.captions.modeLower, layout.captions,
                                                QStringLiteral("character rows: module label table @0x%1 (ids %2); function rows: measured reference (audit 4.2)")
                                                    .arg(lower->offset, 0, 16).arg(lowerIds));
            layout.pages[1] = makeCharacterPage(QStringLiteral("caps"), caps->rows, layout.captions.modeCaps, layout.captions,
                                                QStringLiteral("character rows: module label table @0x%1 (ids %2); function rows: measured reference (audit 4.2)")
                                                    .arg(caps->offset, 0, 16).arg(capsIds));
            layout.moduleRows = true;
            layout.anchors.insert(QStringLiteral("module.lower.ids"), lowerIds);
            layout.anchors.insert(QStringLiteral("module.caps.ids"), capsIds);
        } else {
            layout.provenanceChain << QStringLiteral("rows: module label groups not found; committed row strings used");
        }

        PaletteText paletteText;
        if (scanPaletteText(module, segments, paletteText)) {
            layout.palette.pageFlag = paletteText.flag;
            layout.palette.symbols = paletteText.symbols;
            layout.palette.extras = paletteText.extras;
            layout.palette.provenance = QStringLiteral("module string @0x%1; one-character page flag '%2' + %3 symbols")
                                            .arg(paletteText.offset, 0, 16).arg(paletteText.flag).arg(int(paletteText.symbols.size()));
            layout.paletteFromModule = true;
        } else {
            layout.provenanceChain << QStringLiteral("palette: module string not found; committed 32 symbols used");
        }
    } else {
        layout.provenanceChain << QStringLiteral("module: %1; committed row strings and palette used with the RCO captions/geometry").arg(moduleError);
        qInfo().noquote() << "osk: module unavailable (" << moduleError << "); row strings and palette from committed table, RCO captions/geometry/textures kept";
    }

    recountKeys(layout);
    layout.cost.moduleNs = phase.nsecsElapsed();
    layout.cost.totalNs = total.nsecsElapsed();

    layout.provenance = QStringLiteral("firmware: rco=oskfullkeypanel_plugin (%1 objects, %2 images, %3 strings, %4 B); module=%5 (%6 B, sha256 %7); rows=%8; palette=%9; captions=%10; textures=%11/52 named, %12 decoded; geometry=measured reference (RCO panel planes width=0)")
                            .arg(objectCount).arg(imageCount).arg(int(textRows.size())).arg(container.fileBytes)
                            .arg(moduleOk ? QStringLiteral("oskfullkeypanel_plugin.prx") : QStringLiteral("absent"))
                            .arg(int(module.size())).arg(shortSha(moduleSha))
                            .arg(layout.moduleRows ? QStringLiteral("module label table") : QStringLiteral("committed"))
                            .arg(layout.paletteFromModule ? QStringLiteral("module string") : QStringLiteral("committed"))
                            .arg(layout.captionsFromRco ? QStringLiteral("RCO text table") : QStringLiteral("committed/partial"))
                            .arg(layout.cost.texturesResolved).arg(layout.cost.texturesDecoded);
    layout.provenanceChain
        << QStringLiteral("rco: oskfullkeypanel_plugin version=0x%1 objects=%2 images=%3 strings=%4 bytes=%5")
               .arg(container.version, 0, 16).arg(objectCount).arg(imageCount).arg(int(textRows.size())).arg(container.fileBytes)
        << QStringLiteral("rco objects: fullkey_osk_init_point, fullkey_list_base, fullkey_osk_panel_base, symbol_palette_bg (%1), symbol_palette_group (%2 sym_btn), fullkey_list_text (Text material)")
               .arg(layout.anchors.value(QStringLiteral("symbol_palette_bg"), QStringLiteral("unread")))
               .arg(layout.anchors.value(QStringLiteral("symbol_palette_group.sym_btn"), QStringLiteral("unread")))
        << QStringLiteral("geometry: measured reference section 4 (the RCO panel planes carry width=0; the module assigns them at runtime)")
        << QStringLiteral("module: oskfullkeypanel_plugin.prx bytes=%1 sha256=%2").arg(int(module.size())).arg(moduleSha)
        << QStringLiteral("rows: label table lower %1; caps %2")
               .arg(layout.anchors.value(QStringLiteral("module.lower.ids"), QStringLiteral("unread")))
               .arg(layout.anchors.value(QStringLiteral("module.caps.ids"), QStringLiteral("unread")))
        << QStringLiteral("palette: %1").arg(layout.palette.provenance)
        << QStringLiteral("captions: %1").arg(layout.captions.source)
        << QStringLiteral("textures: %1/52 names in the RCO image table; %2 default-surface, %3 auxiliary (%4); %5 decoded through Assets::image (the rest use text fallbacks)")
               .arg(layout.cost.texturesResolved).arg(layout.defaultSurfaceTextureCount)
               .arg(int(layout.auxiliaryTextures.size())).arg(layout.auxiliaryTextures.join(QStringLiteral(", ")))
               .arg(layout.cost.texturesDecoded);

    qInfo().noquote() << "osk: layout from firmware" << layout.provenance;
    if (trace) traceLayout(layout);
    return layout;
}

} // namespace xmb
