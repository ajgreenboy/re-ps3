#include "rco_objects.h"
#include "qrc.h"
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace xmb {

// --- Container::Data (kept out of the header so callers see an incomplete type) -------------------
struct RcoObjects::Container::Data {
    QByteArray file, inflated;
    bool compressed = false;
    quint32 labels = 0, labelLength = 0;
    quint32 textData = 0, textLength = 0;
    quint32 languageRoot = 0xFFFFFFFFu, imageRoot = 0xFFFFFFFFu, fontRoot = 0xFFFFFFFFu;
    mutable bool textsDone = false, imagesDone = false, fontsDone = false;
    mutable QVector<TextRow> texts;
    mutable QHash<quint32, QString> imageByOffset;
    mutable QHash<quint32, QString> fontByOffset;
};

namespace {

using Value = RcoObjects::Value;
using ValueKind = RcoObjects::ValueKind;
using RefKind = RcoObjects::RefKind;

constexpr quint32 kNone = 0xFFFFFFFFu;
constexpr quint32 kRcoMagic = 0x46525000u;   // the file's first word: 'F' 'R' 'P' NUL
constexpr quint64 kMaxFileBytes = 64ull * 1024 * 1024;
constexpr quint64 kMaxTableBytes = 32ull * 1024 * 1024;
constexpr int kMaxEntries = 100000;           // object + animation entries per container
constexpr int kMaxDepth = 128;
constexpr quint32 kMaxChildren = 65536;
constexpr quint32 kMaxLanguages = 64;
constexpr quint32 kMaxTextRows = 65536;
constexpr quint32 kMaxImages = 4096;

void range(const QByteArray& bytes, quint64 offset, quint64 size) {
    if (offset > quint64(bytes.size()) || size > quint64(bytes.size()) - offset)
        throw std::runtime_error("RCO offset exceeds its container");
}
quint16 be16(const QByteArray& bytes, quint64 at) { range(bytes, at, 2); return qFromBigEndian<quint16>(bytes.constData() + at); }
quint32 be32(const QByteArray& bytes, quint64 at) { range(bytes, at, 4); return qFromBigEndian<quint32>(bytes.constData() + at); }
// zlib stream framed with its expanded size, exactly as Assets::readContainer reads the RCO body.
QByteArray inflateSection(const QByteArray& bytes, quint64 at, quint32 packed, quint32 expanded) {
    if (!expanded || expanded > kMaxTableBytes) throw std::runtime_error("RCO expansion exceeds 32 MiB");
    range(bytes, at, packed);
    QByteArray framed(4, Qt::Uninitialized);
    qToBigEndian(expanded, framed.data());
    framed.append(bytes.constData() + at, packed);
    const auto result = qUncompress(framed);
    if (result.size() != expanded) throw std::runtime_error("RCO zlib length mismatch");
    return result;
}

// The property schema. Field strings are the format's attribute definition in stored order; the type
// letter gives both the width and the typed accessor. See audit/RCO-OBJECT-MODEL.md 3.1.
struct Schema {
    quint16 typeId = 0;
    const char* name = "";
    const char* fields = "";
    int bytes = 0;
};
const Schema kObjectSchemas[] = {
    {0x800, "ObjectTree",
        "", 0},
    {0x801, "Page",
        "unknownInt0:i,onInit:e,onCancel:e,onContextMenu:e,onActivate:e", 36},
    {0x802, "Plane",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,image:g,unknownInt24:i", 100},
    {0x803, "Button",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,image:g,shadow:g,image2:g,unknownRef29:r,unknownEvent"
        "31:e,unknownEvent33:e,unknownEvent35:e,unknownEvent37:e,unknownEvent39:e,unknownEvent41:e,unknow"
        "nEvent43:e,unknownEvent45:e,unknownInt47:i", 188},
    {0x804, "XMenu",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,menus:i,onEnter:e,onContext:e,unknownEvent28:e,onLabe"
        "lLoad:e,unknownRef32:r", 132},
    {0x805, "XMList",
        "unknown0:u,image:g,unknownRef3:r", 20},
    {0x806, "XList",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownInt23:i,onMove:e,onScrollIn:e,onScrollOut:e,on"
        "Push:e,onContext:e,onCancel:e,unknownEvent36:e", 148},
    {0x807, "Progress",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownFloat23:f,unknown24:u,unknownRef25:r,unknownRe"
        "f27:r,unknownRef29:r", 120},
    {0x808, "Scroll",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownFloat23:f,unknownFloat24:f,unknown25:u,unknown"
        "Ref26:r,unknownRef28:r,unknownRef30:r,unknownRef32:r,unknownRef34:r", 140},
    {0x809, "MList",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknown23:u,unknown24:u,unknown25:i,unknown26:u,unkno"
        "wn27:u,unknown28:u,unknown29:u,unknown30:u,unknownRef32:r,unknownRef33:r,unknownRef35:r,unknownR"
        "ef37:r,unknownRef39:r,unknownRef41:r,unknownRef43:r,unknownRef45:r,unknownRef47:r,unknownRef49:r"
        ",unknownRef51:r", 208},
    {0x80a, "MItem",
        "text:t,altText:t,unknownRef4:r", 24},
    {0x80b, "ObjUnknown0xB",
        "", 0},
    {0x80c, "XItem",
        "image:g,text:t,unknownRef4:r", 24},
    {0x80d, "Text",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,text:t,font:r,unknownInt27:i,unknownInt29:i,size:f,to"
        "pRed:f,topGreen:f,topBlue:f,bottomRed:f,bottomGreen:f,bottomBlue:f,spacingHorizontal:f,unknown37"
        ":u,unknown38:u,unknown39:u,spacingVertical:f,shadowX:f,shadowY:f,shadowPerspective:f,shadowRed:f"
        ",shadowGreen:f,shadowBlue:f,shadowAlpha:f,unknown48:u,unknown49:u,unknown50:u,unknownFloat51:f,u"
        "nknownFloat52:f,unknownFloat53:f,unknownFloat54:f,unknown55:u,unknownInt56:i,unknownInt57:i,unkn"
        "own58:u", 232},
    {0x80e, "ModelObject",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,model:m", 96},
    {0x80f, "Spin",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknown23:u,unknownInt24:i,unknownRef25:r,unknownRef2"
        "7:r,unknownEvent29:e,unknownEvent31:e,unknownEvent33:e,unknownRef35:r,unknownRef37:r,unknownRef3"
        "9:r,unknownRef41:r,unknownRef43:r,unknownRef45:r,unknownRef47:r", 192},
    {0x810, "Action",
        "unknownRef0:r", 8},
    {0x811, "ItemSpin",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownInt23:i,unknownInt24:i,unknownInt25:i,unknownI"
        "nt26:i,unknownInt27:i,unknownFloat28:f,unknownRef29:r,unknownRef31:r,unknownEvent33:e,unknownEve"
        "nt35:e,unknownRef37:r,unknownRef39:r,unknownRef41:r,unknownRef43:r,unknownRef45:r,unknownRef47:r"
        ",objPrev:o,objNext:o", 208},
    {0x812, "Group",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u", 88},
    {0x813, "LList",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownInt23:i,unknown24:u,unknownFloat25:f,unknownUn"
        "k26:u,unknownUnk27:u,unknownRef28:r,unknownRef30:r,unknownRef32:r,unknownRef34:r,unknownRef36:r,"
        "unknownRef38:r,unknownEvent40:e,unknownRef42:r", 172},
    {0x814, "LItem",
        "text:t,unknownRef2:r,unknownRef4:r", 24},
    {0x815, "Edit",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknown23:u,unknown24:u,unknown25:u,unknown26:u,unkno"
        "wnRef27:r,unknownRef29:r,unknownEvent31:e,unknownRef33:r,unknownRef35:r,unknownRef37:r,unknownEv"
        "ent39:e,unknownEvent41:e,unknownObject43:o,unknownObject45:o,unknownRef47:r", 192},
    {0x816, "Clock",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownInt23:i,unknownFloat24:f,unknownText25:t,unkno"
        "wnText27:t,unknownRef29:r,unknownRef31:r,unknownEvent33:e,unknownEvent35:e,unknownRef37:r,unknow"
        "nRef39:r,unknownEvent41:e,unknownEvent43:e,unknownRef45:r,unknownRef47:r,unknownEvent49:e", 200},
    {0x817, "IList",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknown23:u,unknown24:u,unknownRef25:r,unknownRef27:r"
        ",unknownEvent29:e,unknownRef31:r,unknownRef33:r,unknownRef35:r,unknownRef37:r", 152},
    {0x818, "IItem",
        "unknown1:u,textDefault:t,textError:t", 20},
    {0x819, "Icon",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownImage23:g,unknownImage25:g,unknownImage27:g", 112},
    {0x81a, "UButton",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,image:g,onPush:e,onFocusIn:e,onFocusOut:e,onLeft:r,on"
        "Right:r,onUp:r,onDown:r,unknownInt39:i", 156},
    {0x81b, "ObjUnknown0x1B",
        "", 0},
    {0x81c, "CheckboxGroup",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknown23:u,unknown24:u,unknown25:u,unknown26:u,unkno"
        "wn27:u,unknown28:u,unknown29:u,unknown30:u,unknownRef31:r,unknownRef33:r,unknownRef35:r,unknownR"
        "ef37:r,unknownRef39:r,unknownRef41:r,unknownRef43:r,unknownRef45:r,unknownRef47:r,unknownRef49:r"
        ",unknownRef51:r", 208},
    {0x81d, "CheckboxItem",
        "unknownRef1:r,unknownRef3:r,unknownRef5:r", 24},
    {0x81e, "Meter",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknown23:u,unknown24:u,unknown25:u,unknown26:u,unkno"
        "wn27:u,unknownRef28:r,unknownRef30:r,unknownRef32:r,unknownRef34:r", 140},
    {0x81f, "EditBox",
        "posX:f,posY:f,posZ:f,redScale:f,greenScale:f,blueScale:f,alphaScale:f,width:f,height:f,depth:f,s"
        "caleWidth:f,scaleHeight:f,scaleDepth:f,iconOffset:i,onLoad:e,unknown17:u,unknown18:u,unknown19:u"
        ",unknownInt20:i,unknownInt21:i,unknown22:u,unknownRef23:r,unknownRef25:r,unknown27:u,unknown28:u"
        ",unknown29:u,unknownFloat30:f,unknownFloat31:f,unknownFloat32:f,unknownFloat33:f,unknownFloat34:"
        "f,unknownFloat35:f,unknown36:u,unknown37:u,unknown38:u,unknown39:u,unknown40:u,unknownFloat41:f,"
        "unknown42:u,unknown43:u,unknownFloat44:f,unknownFloat45:f,unknownFloat46:f,unknownFloat47:f,unkn"
        "own48:u,unknown49:u,unknown50:u,unknownFloat51:f,unknownFloat52:f,unknownFloat53:f,unknownFloat5"
        "4:f,unknown55:u,unknown56:u,unknown57:u,unknown58:u,unknownRef59:r,unknownRef61:r,unknownRef63:r"
        ",unknownRef65:r,unknownRef67:r,unknownRef69:r,unknownRef71:r,unknownRef73:r,unknownRef75:r", 304},
};
const Schema kAnimationSchemas[] = {
    {0x900, "AnimTree",
        "", 0},
    {0x901, "Animation",
        "", 0},
    {0x902, "MoveTo",
        "object:r,duration:f,accelMode:i,x:f,y:f,z:f,unknownInt5:i,unknownInt6:i,unknown7:u", 40},
    {0x903, "Recolour",
        "object:r,duration:f,accelMode:i,red:f,green:f,blue:f,alpha:f", 32},
    {0x904, "Rotate",
        "object:r,duration:f,accelMode:i,x:f,y:f,z:f", 28},
    {0x905, "Resize",
        "object:r,duration:f,accelMode:i,width:f,height:f,depth:f", 28},
    {0x906, "Fade",
        "object:r,duration:f,accelMode:i,transparency:f", 20},
    {0x907, "Delay",
        "time:f", 4},
    {0x908, "FireEvent",
        "event:r", 8},
    {0x909, "Lock",
        "unknownInt0:i", 4},
    {0x90a, "Unlock",
        "unknownInt0:i", 4},
    {0x90b, "SlideOut",
        "object:r,duration:f,accelMode:i,xspeed:f,yspeed:f,xcompress:f,ycompress:f", 32},
};

const Schema* findSchema(const Schema* table, int count, quint16 typeId) {
    for (int i = 0; i < count; ++i) if (table[i].typeId == typeId) return &table[i];
    return nullptr;
}
const Schema* objectSchema(quint16 typeId) { return findSchema(kObjectSchemas, int(std::size(kObjectSchemas)), typeId); }
const Schema* animationSchema(quint16 typeId) { return findSchema(kAnimationSchemas, int(std::size(kAnimationSchemas)), typeId); }
int fieldWidth(char type) { return type == 'f' || type == 'i' || type == 'u' ? 4 : 8; }

// Visit every named field of a schema without allocating: name, type letter, byte offset.
template <typename Fn>
void eachField(const Schema& schema, Fn&& fn) {
    const char* at = schema.fields;
    int offset = 0;
    while (*at) {
        const char* comma = std::strchr(at, ',');
        const size_t length = comma ? size_t(comma - at) : std::strlen(at);
        const char* colon = static_cast<const char*>(std::memchr(at, ':', length));
        if (!colon) break;
        fn(QString::fromLatin1(at, int(colon - at)), colon[1], offset);
        offset += fieldWidth(colon[1]);
        if (!comma) break;
        at = comma + 1;
    }
}

Value readValue(const char* bytes, int size, int offset, char type) {
    Value value;
    if (offset < 0 || offset + fieldWidth(type) > size) return value;
    const quint32 word = qFromBigEndian<quint32>(bytes + offset);
    const quint32 second = fieldWidth(type) == 8 ? qFromBigEndian<quint32>(bytes + offset + 4) : 0;
    switch (type) {
    case 'f': value.kind = ValueKind::Float; value.real = std::bit_cast<float>(word); break;
    case 'i': value.kind = ValueKind::Int; value.integer = qint32(word); break;
    case 'u': value.kind = ValueKind::Unknown; value.integer = qint64(word); break;
    case 'r': value.kind = ValueKind::Ref; value.tag = word; value.target = second; break;
    case 'e': value.kind = ValueKind::Event; value.tag = word; value.target = second; break;
    case 'g': value.kind = ValueKind::Image; value.tag = word; value.target = second; break;
    case 't': value.kind = ValueKind::Text; value.tag = word; value.target = second; break;
    case 'm': value.kind = ValueKind::Model; value.tag = word; value.target = second; break;
    case 'o': value.kind = ValueKind::Object; value.tag = word; value.target = second; break;
    default: break;
    }
    return value;
}

QString readLabel(const QByteArray& tables, const RcoObjects::Container::Data& data, quint32 offset) {
    if (offset == kNone || !data.labelLength) return {};
    if (quint64(offset) >= data.labelLength) throw std::runtime_error("RCO label is out of range");
    const quint64 at = quint64(data.labels) + offset;
    if (at >= quint64(tables.size())) throw std::runtime_error("RCO label exceeds its table");
    const qsizetype end = tables.indexOf('\0', qsizetype(at));
    if (end < 0 || quint64(end) >= quint64(data.labels) + data.labelLength)
        throw std::runtime_error("Unterminated RCO label");
    return QString::fromUtf8(tables.constData() + at, end - qsizetype(at));
}

QString decodeText(const QByteArray& payload, quint32 offset, quint32 length, quint16 format) {
    if (quint64(offset) > quint64(payload.size()) || quint64(length) > quint64(payload.size()) - offset) return {};
    const QByteArrayView bytes(payload.constData() + offset, length);
    QString value;
    if (format == 0) value = QString::fromUtf8(bytes);
    else if (format == 1) { QStringDecoder decoder(QStringDecoder::Utf16BE); value = decoder(bytes); if (decoder.hasError()) return {}; }
    else if (format == 2) { QStringDecoder decoder(QStringDecoder::Utf32BE); value = decoder(bytes); if (decoder.hasError()) return {}; }
    else return {};
    while (value.endsWith(QChar(0))) value.chop(1);
    return value;
}

// The image tree: 0x0401 entries, label at +4, next-sibling distance at +20 (the same walk
// Assets::readContainer uses for images). Cached per container; a malformed table yields no names.
void loadImages(const RcoObjects::Container& container) {
    auto& data = *container.data;
    if (data.imagesDone) return;
    data.imagesDone = true;
    if (data.imageRoot == kNone) return;
    try {
        const QByteArray& tables = container.tables();
        const quint32 count = be32(tables, quint64(data.imageRoot) + 16);
        if (count > kMaxImages) return;
        quint64 at = quint64(data.imageRoot) + 0x28;
        for (quint32 i = 0; i < count; ++i) {
            range(tables, at, 0x28);
            const quint16 kind = be16(tables, at);
            const quint32 labelOffset = be32(tables, at + 4);
            const quint32 next = be32(tables, at + 20);
            if (kind == 0x0401) data.imageByOffset.insert(quint32(at), readLabel(tables, data, labelOffset));
            if (!next || next < 0x28) break;
            at += next;
        }
    } catch (const std::exception&) {
        data.imageByOffset.clear();
    }
}

// The font tree (header 0x2C): 0x0701 entries, label at +4, next-sibling distance at +20. The Text
// object's `font` reference points at one of these entries (tag 0x0405).
void loadFonts(const RcoObjects::Container& container) {
    auto& data = *container.data;
    if (data.fontsDone) return;
    data.fontsDone = true;
    if (data.fontRoot == kNone) return;
    try {
        const QByteArray& tables = container.tables();
        const quint32 count = be32(tables, quint64(data.fontRoot) + 16);
        if (count > kMaxImages) return;
        quint64 at = quint64(data.fontRoot) + 0x28;
        for (quint32 i = 0; i < count; ++i) {
            range(tables, at, 0x28);
            const quint16 kind = be16(tables, at);
            const quint32 labelOffset = be32(tables, at + 4);
            const quint32 next = be32(tables, at + 20);
            if (kind == 0x0701) data.fontByOffset.insert(quint32(at), readLabel(tables, data, labelOffset));
            if (!next || next < 0x28) break;
            at += next;
        }
    } catch (const std::exception&) {
        data.fontByOffset.clear();
    }
}

void walkTree(RcoObjects::Container& container, const QByteArray& tables, quint32 root, bool objects) {
    struct Walker {
        RcoObjects::Container& container;
        const QByteArray& tables;
        bool objects;
        int count = 0;
        quint32 walk(quint32 at, int depth, int parent, int& id) {
            if (++count > kMaxEntries) throw std::runtime_error("RCO entry count exceeds its limit");
            if (depth > kMaxDepth) throw std::runtime_error("RCO entry nesting exceeds its limit");
            range(tables, at, 40);
            const quint16 typeId = be16(tables, at);
            const quint32 labelOffset = be32(tables, at + 4);
            const quint32 rawSize = be32(tables, at + 12);
            const quint32 children = be32(tables, at + 16);
            const Schema* schema = objects ? objectSchema(typeId) : animationSchema(typeId);
            if (!rawSize && !schema) throw std::runtime_error("Unknown RCO entry type without an explicit size");
            const quint32 size = rawSize ? rawSize : quint32(40 + schema->bytes);
            if (size < 40) throw std::runtime_error("Invalid RCO entry size");
            if (quint64(at) + size > quint64(tables.size())) throw std::runtime_error("RCO entry exceeds its table");
            if (children > kMaxChildren) throw std::runtime_error("RCO child count exceeds its limit");
            QString name;
            if (labelOffset != kNone && container.data->labelLength) name = readLabel(tables, *container.data, labelOffset);
            const QString typeName = schema ? QString::fromLatin1(schema->name) : QStringLiteral("0x%1").arg(typeId, 0, 16);
            id = int(objects ? container.objects.size() : container.animations.size());
            if (objects) {
                RcoObjects::Object entry;
                entry.id = id; entry.parent = parent; entry.depth = depth;
                entry.offset = at; entry.typeId = typeId; entry.size = size;
                entry.type = typeName; entry.name = name;
                if (!name.isEmpty() && !container.objectByName.contains(name)) container.objectByName.insert(name, id);
                container.objectByOffset.insert(at, id);
                container.objects.push_back(entry);
            } else {
                RcoObjects::Animation entry;
                entry.id = id; entry.parent = parent; entry.depth = depth;
                entry.offset = at; entry.typeId = typeId; entry.size = size;
                entry.type = typeName; entry.name = name;
                container.animations.push_back(entry);
            }
            quint32 child = at + size;
            for (quint32 i = 0; i < children; ++i) {
                int childId = -1;
                const quint32 end = walk(child, depth + 1, id, childId);
                const quint32 next = be32(tables, quint64(child) + 20);
                if (i + 1 < children && next && next < 40) throw std::runtime_error("Invalid RCO sibling offset");
                child = next ? child + next : end;
            }
            return std::max(quint32(at + size), child);
        }
    } walker{container, tables, objects};
    int id = -1;
    walker.walk(root, 0, -1, id);
}

void parseContainer(RcoObjects::Container& container) {
    auto data = std::make_shared<RcoObjects::Container::Data>();
    container.data = data;
    QFile source(container.path);
    if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open local RCO");
    if (quint64(source.size()) > kMaxFileBytes) throw std::runtime_error("RCO exceeds 64 MiB");
    data->file = source.readAll();
    if (data->file.size() < 0xa4) throw std::runtime_error("RCO header is truncated");
    if (be32(data->file, 0) != kRcoMagic) throw std::runtime_error("Expected PS3 big-endian RCO");
    container.version = be32(data->file, 4);
    container.fileBytes = quint32(data->file.size());
    const quint32 compression = be32(data->file, 12) >> 4;
    if (compression > 1) throw std::runtime_error("Unsupported RCO header compression");
    container.compression = int(compression);
    data->compressed = compression != 0;
    if (compression == 1) {
        const quint32 packed = be32(data->file, 0xa4);
        const quint32 expanded = be32(data->file, 0xa8);
        data->inflated = data->file.left(0xa4) + inflateSection(data->file, 0xb0, packed, expanded);
    }
    const QByteArray& tables = container.tables();
    container.tableBytes = quint32(tables.size());
    data->labels = be32(data->file, 0x40);
    data->labelLength = be32(data->file, 0x44);
    if (data->labels == kNone || !data->labelLength) data->labels = data->labelLength = 0;
    else range(tables, data->labels, data->labelLength);
    data->languageRoot = be32(data->file, 0x18);
    data->imageRoot = be32(data->file, 0x24);
    data->fontRoot = be32(data->file, 0x2c);
    data->textData = be32(data->file, 0x38);
    data->textLength = be32(data->file, 0x3c);
    const quint32 objectRoot = be32(data->file, 0x30);
    const quint32 animationRoot = be32(data->file, 0x34);
    if (objectRoot != kNone) walkTree(container, tables, objectRoot, true);
    if (animationRoot != kNone) walkTree(container, tables, animationRoot, false);
    for (const auto& entry : container.objects)
        if (entry.parent >= 0) container.objects[entry.parent].children.push_back(entry.id);
    for (const auto& entry : container.animations)
        if (entry.parent >= 0) container.animations[entry.parent].children.push_back(entry.id);
    container.ok = true;
}

// --- MNU files -----------------------------------------------------------------------------------
void parseMnu(const QByteArray& bytes, Materials::Mnu& mnu) {
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int first = line.indexOf(':');
        if (first <= 0) continue;
        const int second = line.indexOf(':', first + 1);
        if (second < 0) continue;
        const QString name = line.left(first).trimmed();
        const QString type = line.mid(first + 1, second - first - 1).trimmed().toLower();
        const QString text = line.mid(second + 1).trimmed();
        if (name.isEmpty()) continue;
        Materials::Item item;
        if (type == "float" || type == "int") {
            bool ok = false;
            const double number = text.toDouble(&ok);
            if (!ok) continue;
            item.kind = Materials::Item::Kind::Number;
            item.number = number;
        } else {
            item.kind = Materials::Item::Kind::Text;
            item.text = text;
        }
        mnu.entries.append({name, item});
        mnu.byName.insert(name, item);
    }
}

} // namespace

const QByteArray& RcoObjects::Container::tables() const {
    static const QByteArray empty;
    if (!data) return empty;
    return data->inflated.isEmpty() ? data->file : data->inflated;
}

const RcoObjects::Object* RcoObjects::Container::object(const QString& key) const {
    const auto it = objectByName.constFind(key);
    return it == objectByName.cend() ? nullptr : &objects.at(it.value());
}
const RcoObjects::Object* RcoObjects::Container::objectAt(quint32 offset) const {
    const auto it = objectByOffset.constFind(offset);
    return it == objectByOffset.cend() ? nullptr : &objects.at(it.value());
}
const RcoObjects::Object* RcoObjects::Container::parent(const Object& entry) const {
    return entry.parent < 0 || entry.parent >= objects.size() ? nullptr : &objects.at(entry.parent);
}
QVector<const RcoObjects::Object*> RcoObjects::Container::children(const Object& entry) const {
    QVector<const Object*> result;
    for (int id : entry.children) if (id >= 0 && id < objects.size()) result.push_back(&objects.at(id));
    return result;
}
const RcoObjects::Animation* RcoObjects::Container::animation(const QString& key) const {
    for (const auto& entry : animations) if (entry.name == key) return &entry;
    return nullptr;
}

QVector<RcoObjects::Property> RcoObjects::Container::properties(const Object& entry) const {
    QVector<Property> result;
    const Schema* schema = objectSchema(entry.typeId);
    if (!schema || !data) return result;
    const QByteArray& bytes = tables();
    const quint64 header = quint64(entry.offset) + 40;
    if (header > quint64(bytes.size()) || quint64(entry.offset) + entry.size > quint64(bytes.size())) return result;
    const int size = int(entry.size - 40);
    const char* extra = bytes.constData() + header;
    eachField(*schema, [&](const QString& name, char type, int offset) {
        if (offset + fieldWidth(type) > size) return;
        Property property;
        property.name = name;
        property.value = readValue(extra, size, offset, type);
        result.push_back(std::move(property));
    });
    return result;
}

QVector<RcoObjects::Property> RcoObjects::Container::properties(const Animation& entry) const {
    QVector<Property> result;
    const Schema* schema = animationSchema(entry.typeId);
    if (!schema || !data) return result;
    const QByteArray& bytes = tables();
    const quint64 header = quint64(entry.offset) + 40;
    if (header > quint64(bytes.size()) || quint64(entry.offset) + entry.size > quint64(bytes.size())) return result;
    const int size = int(entry.size - 40);
    const char* extra = bytes.constData() + header;
    eachField(*schema, [&](const QString& name, char type, int offset) {
        if (offset + fieldWidth(type) > size) return;
        Property property;
        property.name = name;
        property.value = readValue(extra, size, offset, type);
        result.push_back(std::move(property));
    });
    return result;
}

bool RcoObjects::Container::property(const Object& entry, const QString& key, Value& out) const {
    const Schema* schema = objectSchema(entry.typeId);
    if (!schema || !data) return false;
    const QByteArray& bytes = tables();
    const quint64 header = quint64(entry.offset) + 40;
    if (header > quint64(bytes.size()) || quint64(entry.offset) + entry.size > quint64(bytes.size())) return false;
    const int size = int(entry.size - 40);
    const char* extra = bytes.constData() + header;
    bool found = false;
    eachField(*schema, [&](const QString& name, char type, int offset) {
        if (found || name != key) return;
        if (offset + fieldWidth(type) > size) return;
        out = readValue(extra, size, offset, type);
        found = true;
    });
    return found;
}

double RcoObjects::Container::number(const Object& entry, const QString& key, double fallback) const {
    Value value;
    if (!property(entry, key, value)) return fallback;
    if (value.kind == ValueKind::Float) return value.real;
    if (value.kind == ValueKind::Int || value.kind == ValueKind::Unknown) return double(value.integer);
    return fallback;
}
qint64 RcoObjects::Container::integer(const Object& entry, const QString& key, qint64 fallback) const {
    Value value;
    if (!property(entry, key, value)) return fallback;
    if (value.kind == ValueKind::Int || value.kind == ValueKind::Unknown) return value.integer;
    if (value.kind == ValueKind::Float) return qint64(value.real);
    return fallback;
}

QByteArray RcoObjects::Container::extra(const Object& entry) const {
    const QByteArray& bytes = tables();
    if (!data || quint64(entry.offset) + entry.size > quint64(bytes.size()) || entry.size < 40) return {};
    return bytes.mid(qsizetype(entry.offset) + 40, qsizetype(entry.size) - 40);
}
QByteArray RcoObjects::Container::extra(const Animation& entry) const {
    const QByteArray& bytes = tables();
    if (!data || quint64(entry.offset) + entry.size > quint64(bytes.size()) || entry.size < 40) return {};
    return bytes.mid(qsizetype(entry.offset) + 40, qsizetype(entry.size) - 40);
}
QVector<float> RcoObjects::Container::floats(const Object& entry) const { return floatsOf(extra(entry)); }
QVector<float> RcoObjects::Container::floats(const Animation& entry) const { return floatsOf(extra(entry)); }

QVector<float> RcoObjects::Container::floatsOf(const QByteArray& bytes) {
    QVector<float> result;
    result.reserve(bytes.size() / 4);
    for (qsizetype at = 0; at + 4 <= bytes.size(); at += 4)
        result.push_back(std::bit_cast<float>(qFromBigEndian<quint32>(bytes.constData() + at)));
    return result;
}

QString RcoObjects::Container::label(quint32 offset) const {
    if (!data || !data->labelLength || offset == kNone) return {};
    const QByteArray& bytes = tables();
    if (quint64(offset) >= data->labelLength) return {};
    const quint64 at = quint64(data->labels) + offset;
    if (at >= quint64(bytes.size())) return {};
    const qsizetype end = bytes.indexOf('\0', qsizetype(at));
    if (end < 0 || quint64(end) >= quint64(data->labels) + data->labelLength) return {};
    return QString::fromUtf8(bytes.constData() + at, end - qsizetype(at));
}

const QVector<RcoObjects::TextRow>& RcoObjects::Container::textRows() const {
    static const QVector<TextRow> empty;
    if (!data) return empty;
    auto& cache = *data;
    if (cache.textsDone) return cache.texts;
    cache.textsDone = true;
    if (cache.languageRoot == kNone || !cache.labelLength) return cache.texts;
    try {
        const QByteArray& tables = this->tables();
        const quint32 languages = be32(tables, quint64(cache.languageRoot) + 16);
        if (languages > kMaxLanguages) return cache.texts;
        quint64 at = quint64(cache.languageRoot) + 0x28;
        quint16 format = 0;
        quint32 count = 0;
        quint64 rowsAt = 0;
        bool found = false;
        for (quint32 i = 0; i < languages && !found; ++i) {
            range(tables, at, 0x30);
            const quint64 info = at + 0x28;
            if (be16(tables, info) == 1) {
                format = be16(tables, info + 2);
                count = be32(tables, info + 4);
                if (count > kMaxTextRows) throw std::runtime_error("RCO text row count exceeds its limit");
                rowsAt = info + 8;
                range(tables, rowsAt, quint64(count) * 12);
                found = true;
                break;
            }
            const quint32 next = be32(tables, at + 20);
            if (!next) break;
            if (next < 0x30) throw std::runtime_error("Invalid RCO language sibling offset");
            at += next;
        }
        if (!found) return cache.texts;
        QByteArray english;
        if (cache.textLength) {
            range(cache.file, cache.textData, cache.textLength);
            if (!cache.compressed) english = cache.file.mid(cache.textData, cache.textLength);
            else {
                quint64 chunk = cache.textData;
                for (int i = 0; i < 64; ++i) {
                    range(cache.file, chunk, 16);
                    if (chunk + 16 > quint64(cache.textData) + cache.textLength) break;
                    const quint16 language = be16(cache.file, chunk);
                    const quint32 next = be32(cache.file, chunk + 4);
                    const quint32 packed = be32(cache.file, chunk + 8);
                    if (packed > quint64(cache.textData) + cache.textLength - chunk - 16) break;
                    if (language == 1) {
                        english = inflateSection(cache.file, chunk + 16, packed, be32(cache.file, chunk + 12));
                        break;
                    }
                    if (!next) break;
                    if (next < 16 + quint64(packed)) break;
                    chunk += next;
                }
            }
        }
        cache.texts.reserve(qsizetype(count));
        for (quint32 i = 0; i < count; ++i) {
            const quint64 row = rowsAt + quint64(i) * 12;
            TextRow entry;
            entry.index = int(i);
            entry.label = label(be32(tables, row));
            const quint32 length = be32(tables, row + 4);
            const quint32 offset = be32(tables, row + 8);
            if (!english.isEmpty()) entry.text = decodeText(english, offset, length, format);
            cache.texts.push_back(std::move(entry));
        }
    } catch (const std::exception& error) {
        qWarning().noquote() << "rco: text table of" << name << error.what();
        cache.texts.clear();
    }
    return cache.texts;
}

const RcoObjects::TextRow* RcoObjects::Container::textAt(quint32 index) const {
    const auto& rows = textRows();
    return index < quint32(rows.size()) ? &rows.at(int(index)) : nullptr;
}
QString RcoObjects::Container::text(quint32 index) const {
    const auto* row = textAt(index);
    return row ? row->text : QString();
}
QString RcoObjects::Container::textLabel(quint32 index) const {
    const auto* row = textAt(index);
    return row ? row->label : QString();
}
QString RcoObjects::Container::imageName(quint32 tableOffset) const {
    loadImages(*this);
    return data ? data->imageByOffset.value(tableOffset) : QString();
}
QHash<quint32, QString> RcoObjects::Container::images() const {
    loadImages(*this);
    return data ? data->imageByOffset : QHash<quint32, QString>{};
}
QString RcoObjects::Container::fontName(quint32 tableOffset) const {
    loadFonts(*this);
    return data ? data->fontByOffset.value(tableOffset) : QString();
}
QHash<quint32, QString> RcoObjects::Container::fonts() const {
    loadFonts(*this);
    return data ? data->fontByOffset : QHash<quint32, QString>{};
}

QString RcoObjects::Container::resolve(const Value& value) const {
    switch (value.refKind()) {
    case RefKind::Null: return QStringLiteral("null");
    case RefKind::Text: {
        const QString name = textLabel(value.target);
        return name.isEmpty() ? QStringLiteral("text#%1").arg(value.target) : QStringLiteral("text#%1:%2").arg(value.target).arg(name);
    }
    case RefKind::Image: {
        const QString name = imageName(value.target);
        return name.isEmpty() ? QStringLiteral("image@0x%1").arg(value.target, 0, 16) : name;
    }
    case RefKind::Font: {
        const QString name = fontName(value.target);
        return name.isEmpty() ? QStringLiteral("font@0x%1").arg(value.target, 0, 16) : name;
    }
    case RefKind::Object: {
        const auto* entry = objectAt(value.target);
        if (!entry) return QStringLiteral("object@0x%1").arg(value.target, 0, 16);
        return entry->name.isEmpty() ? QStringLiteral("(anon)") : entry->name;
    }
    case RefKind::Id: return QStringLiteral("id:0x%1").arg(value.target, 0, 16);
    default: return QStringLiteral("ref:0x%1:0x%2").arg(value.tag, 0, 16).arg(value.target, 0, 16);
    }
}

// --- RcoObjects -----------------------------------------------------------------------------------
RcoObjects::RcoObjects(Assets& assets) : assets_(&assets) {}

const RcoObjects::Container& RcoObjects::container(const QString& name) {
    if (const auto it = containers_.constFind(name); it != containers_.cend()) return *it.value();
    auto container = std::make_shared<Container>();
    container->name = name;
    QElapsedTimer timer;
    timer.start();
    try {
        if (name.isEmpty()) throw std::runtime_error("Empty RCO name");
        if (name.contains('/') || name.contains('\\') || name.contains(QStringLiteral("..")))
            throw std::runtime_error("Invalid RCO name");
        container->path = QDir(QDir(assets_->root()).filePath(QStringLiteral("vsh/resource"))).filePath(name + QStringLiteral(".rco"));
        parseContainer(*container);
    } catch (const std::exception& error) {
        container->ok = false;
        container->error = QString::fromUtf8(error.what());
        container->objects.clear();
        container->animations.clear();
        container->objectByOffset.clear();
        container->objectByName.clear();
        container->data.reset();
        qWarning().noquote() << "rco:" << name << container->error;
    }
    cost_.containers++;
    cost_.nanoseconds += timer.nsecsElapsed();
    if (container->ok) cost_.bytesRead += container->fileBytes;
    containers_.insert(name, container);
    return *container;
}

bool RcoObjects::isLoaded(const QString& name) const { return containers_.contains(name); }

QStringList RcoObjects::containers() const {
    QStringList names;
    const QDir dir(QDir(assets_->root()).filePath(QStringLiteral("vsh/resource")));
    for (const auto& info : dir.entryInfoList({QStringLiteral("*.rco")}, QDir::Files, QDir::Name))
        names << info.completeBaseName();
    return names;
}

QStringList RcoObjects::objectTypes() {
    QStringList result;
    for (const auto& schema : kObjectSchemas)
        result << QStringLiteral("0x%1 %2").arg(schema.typeId, 0, 16).arg(QString::fromLatin1(schema.name));
    for (const auto& schema : kAnimationSchemas)
        result << QStringLiteral("0x%1 %2 (animation)").arg(schema.typeId, 0, 16).arg(QString::fromLatin1(schema.name));
    return result;
}

QString RcoObjects::schemaSource() {
    return QStringLiteral("Rcomage objattribdef-ps3.ini / animattribdef-ps3.ini (MIT): 32 object types, "
                          "12 animation types; field order and widths only, every value is read from the RCO");
}

// --- Materials ------------------------------------------------------------------------------------
Materials::Materials(Assets& assets, RcoObjects& objects) : assets_(&assets), objects_(&objects) {}

const Materials::Mnu& Materials::mnu(const QString& archive, const QString& path) {
    const QString key = archive + QLatin1Char('\n') + path;
    if (const auto it = mnus_.constFind(key); it != mnus_.cend()) return *it.value();
    auto entry = std::make_shared<Mnu>();
    entry->archive = archive;
    entry->path = path;
    entry->name = QFileInfo(path).fileName();
    try {
        parseMnu(assets_->qglFile(archive, path), *entry);
        entry->ok = true;
    } catch (const std::exception& error) {
        entry->error = QString::fromUtf8(error.what());
        qWarning().noquote() << "mnu:" << archive << path << entry->error;
    }
    mnus_.insert(key, entry);
    return *entry;
}

const Materials::Mnu& Materials::icons(const QString& preset) {
    return mnu(QStringLiteral("icons"), preset.isEmpty() ? QStringLiteral("ICONS.mnu")
                                                        : QStringLiteral("override/%1/ICONS.mnu").arg(preset));
}
const Materials::Mnu& Materials::particles(const QString& preset) { return mnu(QStringLiteral("lines"), preset.isEmpty() ? QStringLiteral("PARTICLES.mnu") : QStringLiteral("override/%1/PARTICLES.mnu").arg(preset)); }
const Materials::Mnu& Materials::particlesUi() { return mnu(QStringLiteral("lines"), QStringLiteral("PARTICLES_UI.mnu")); }
const Materials::Mnu& Materials::particlesSpe() { return mnu(QStringLiteral("lines"), QStringLiteral("PARTICLES_SPE.mnu")); }
const Materials::Mnu& Materials::background(const QString& preset) { return mnu(QStringLiteral("lines"), preset.isEmpty() ? QStringLiteral("BACKGROUND.mnu") : QStringLiteral("override/%1/BACKGROUND.mnu").arg(preset)); }
const Materials::Mnu& Materials::line1(const QString& preset) { return mnu(QStringLiteral("lines"), preset.isEmpty() ? QStringLiteral("LINE1.mnu") : QStringLiteral("override/%1/LINE1.mnu").arg(preset)); }
const Materials::Mnu& Materials::hdr(const QString& preset) { return mnu(QStringLiteral("lines"), preset.isEmpty() ? QStringLiteral("HDR.mnu") : QStringLiteral("override/%1/HDR.mnu").arg(preset)); }

QStringList Materials::presets(const QString& archive, const QString& stem) const {
    if (!presets_.contains(archive)) {
        QStringList names;
        try {
            const QDir directory(QDir(assets_->root()).filePath(QStringLiteral("vsh/resource/qgl")));
            QrcArchive qrc(directory.filePath(archive + QStringLiteral(".qrc")));
            names = qrc.names();
        } catch (const std::exception&) {
        }
        presets_.insert(archive, names);
    }
    QStringList result;
    const QString prefix = QStringLiteral("override/");
    const QString suffix = QLatin1Char('/') + stem;
    for (const auto& name : presets_.value(archive)) {
        if (!name.startsWith(prefix) || !name.endsWith(suffix)) continue;
        const QString preset = name.mid(prefix.size(), name.size() - prefix.size() - suffix.size());
        if (!preset.isEmpty() && !preset.contains(QLatin1Char('/')) && !result.contains(preset)) result << preset;
    }
    result.sort();
    return result;
}

Materials::TextMaterial Materials::textMaterial(const QString& containerName, const QString& objectName) {
    TextMaterial result;
    result.container = containerName;
    result.object = objectName;
    const auto& container = objects_->container(containerName);
    if (!container.ok) return result;
    const auto* object = container.object(objectName);
    if (!object) return result;
    result.present = true;
    result.size = container.number(*object, QStringLiteral("size"), 0);
    result.spacingHorizontal = container.number(*object, QStringLiteral("spacingHorizontal"), 0);
    result.spacingVertical = container.number(*object, QStringLiteral("spacingVertical"), 0);
    result.shadowX = container.number(*object, QStringLiteral("shadowX"), 0);
    result.shadowY = container.number(*object, QStringLiteral("shadowY"), 0);
    result.shadowPerspective = container.number(*object, QStringLiteral("shadowPerspective"), 0);
    const auto channel = [&](const char* name, double fallback) {
        return std::clamp(container.number(*object, QString::fromLatin1(name), fallback), 0.0, 1.0);
    };
    result.top = QColor::fromRgbF(channel("topRed", 0), channel("topGreen", 0), channel("topBlue", 0), 1.0);
    result.bottom = QColor::fromRgbF(channel("bottomRed", 0), channel("bottomGreen", 0), channel("bottomBlue", 0), 1.0);
    result.shadow = QColor::fromRgbF(channel("shadowRed", 0), channel("shadowGreen", 0), channel("shadowBlue", 0),
                                     channel("shadowAlpha", 1));
    Value font;
    if (container.property(*object, QStringLiteral("font"), font)) {
        if (font.refKind() == RefKind::Text) {
            const QString label = container.textLabel(font.target);
            result.fontReference = label.isEmpty() ? QStringLiteral("text#%1").arg(font.target) : label;
        } else if (font.refKind() != RefKind::Null) {
            result.fontReference = container.resolve(font);
        }
    }
    return result;
}

// --- Mnu accessors --------------------------------------------------------------------------------
double Materials::Mnu::number(const QString& key, double fallback) const {
    const auto it = byName.constFind(key);
    if (it == byName.cend() || it->kind != Item::Kind::Number) return fallback;
    return it->number;
}
qint64 Materials::Mnu::integer(const QString& key, qint64 fallback) const {
    const auto it = byName.constFind(key);
    if (it == byName.cend() || it->kind != Item::Kind::Number) return fallback;
    return qint64(it->number);
}
QString Materials::Mnu::text(const QString& key, const QString& fallback) const {
    const auto it = byName.constFind(key);
    if (it == byName.cend() || it->kind != Item::Kind::Text) return fallback;
    return it->text;
}
QStringList Materials::Mnu::keys() const {
    QStringList result;
    for (const auto& entry : entries) result << entry.first;
    return result;
}

} // namespace xmb
