#pragma once
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include "assets.h"

namespace xmb {

// P1.1: the RCO object model, read from the firmware's own bytes at run time.
//
// An RCO is a named resource container (vsh/resource/<name>.rco) holding the object tree, the animation
// tree, the image tree, the font tree and the text table. This class decodes the container exactly as
// Assets::readContainer does (same header, same zlib section, same 64 MiB / 32 MiB bounds) and then walks
// the object and animation trees with the format's own attribute schema. Nothing is cached from a build
// tool and no value is typed in: names come from the label section, numbers from the entry bytes, text
// from the localized text table, image/object/font references from the trees they point into.
//
// Cost model: lazy per container. Constructing the reader does no I/O; the first container(name) call for
// a name reads and parses that file once, and later calls return the cached tree (a container is never
// re-read, a failure is cached too). A .rco is read only when a consumer asks for it, so an unused
// container costs nothing at startup. Container::file/tables stay resident so text and reference lookups
// need no further file access. Not thread-safe: one instance belongs to one thread (the UI thread).
// See audit/RCO-OBJECT-MODEL.md for the measured costs.
class RcoObjects final {
public:
    // A property's storage type, from the format's attribute definition.
    enum class ValueKind { Int, Float, Unknown, Ref, Event, Image, Text, Model, Object };
    // What a 4+4 byte reference word points at. The first word is the target table tag, the second the
    // offset or index inside it (see audit/RCO-OBJECT-MODEL.md 3.2). Id is a tag whose table is not
    // identified locally; it keeps the raw value.
    enum class RefKind { Null, Text, Image, Font, Object, Id, Unknown };

    struct Value {
        ValueKind kind = ValueKind::Unknown;
        qint64 integer = 0;   // Int, Unknown (raw u32), and object ids
        double real = 0;      // Float
        quint32 tag = 0;      // reference word 1
        quint32 target = 0;   // reference word 2
        bool isReference() const {
            return kind == ValueKind::Ref || kind == ValueKind::Event || kind == ValueKind::Image ||
                   kind == ValueKind::Text || kind == ValueKind::Model || kind == ValueKind::Object;
        }
        RefKind refKind() const {
            if (!isReference()) return RefKind::Unknown;
            if (tag == 0xffff0000u || tag == 0xffffffffu) return target == 0xffffffffu ? RefKind::Null : RefKind::Unknown;
            switch (tag >> 16) {
            case 0x0401: return RefKind::Text;
            case 0x0402: return RefKind::Image;
            case 0x0405: return RefKind::Font;
            case 0x0407: return RefKind::Object;
            case 0x0409: return RefKind::Object;   // animation-scope object reference
            case 0x0400: return RefKind::Id;
            default: return RefKind::Unknown;
            }
        }
    };

    struct Property {
        QString name;
        Value value;
    };

    // One entry of the object tree (or animation tree). Offsets are absolute in the container's table
    // section; parent/children are indices into Container::objects.
    struct Object {
        int id = -1;          // index in Container::objects
        int parent = -1;      // -1 for the tree root's anonymous head
        int depth = 0;
        quint32 offset = 0;
        quint16 typeId = 0;
        quint32 size = 0;     // 40-byte header + stored bytes
        QString type;         // schema name ("Plane", "Text", ...) or "0x8ff"
        QString name;         // label-section name, empty when the entry has none
        QVector<int> children;
    };
    struct Animation {
        int id = -1;
        int parent = -1;
        int depth = 0;
        quint32 offset = 0;
        quint16 typeId = 0;
        quint32 size = 0;
        QString type;         // "MoveTo", "Fade", ...
        QString name;
        QVector<int> children;
    };

    // One row of the text table: the label the RCO needs for the string, and its English text.
    struct TextRow {
        int index = -1;
        QString label;
        QString text;
    };

    // A parsed RCO. Everything except the lazy text/image tables is built on first use.
    struct Container {
        QString name, path, error;
        bool ok = false;
        quint32 version = 0;
        quint32 fileBytes = 0, tableBytes = 0;
        int compression = 0;
        QVector<Object> objects;
        QVector<Animation> animations;
        QHash<quint32, int> objectByOffset;
        QHash<QString, int> objectByName;
        // Lookups (never throw; an absent name/offset returns nullptr).
        const Object* object(const QString& name) const;
        const Object* objectAt(quint32 offset) const;
        const Object* parent(const Object& entry) const;
        QVector<const Object*> children(const Object& entry) const;
        const Animation* animation(const QString& name) const;
        // Typed properties of one entry, in the schema's stored order.
        QVector<Property> properties(const Object& entry) const;
        QVector<Property> properties(const Animation& entry) const;
        // The value of one named property; false when the entry's schema has no such field.
        bool property(const Object& entry, const QString& key, Value& out) const;
        double number(const Object& entry, const QString& key, double fallback = 0) const;
        qint64 integer(const Object& entry, const QString& key, qint64 fallback = 0) const;
        // The bytes past the 40-byte header (bounded by the stored entry size), and their float series.
        QByteArray extra(const Object& entry) const;
        QByteArray extra(const Animation& entry) const;
        QVector<float> floats(const Object& entry) const;
        QVector<float> floats(const Animation& entry) const;
        // Text and image tables, decoded on first use and then cached.
        const QVector<TextRow>& textRows() const;
        const TextRow* textAt(quint32 index) const;
        QString text(quint32 index) const;               // the English string at a text index
        QString textLabel(quint32 index) const;          // its resource label (never empty when known)
        QString imageName(quint32 tableOffset) const;    // image tree entry name by table offset
        QHash<quint32, QString> images() const;          // every image entry in tree order
        QString fontName(quint32 tableOffset) const;     // font tree entry name by table offset
        QHash<quint32, QString> fonts() const;           // every font entry in tree order
        // Human-readable resolution of a reference value: image name, object name, text label, "id:N",
        // "null" or "0x....". The dump CLI prints these; consumers that need the structured target use
        // refKind()/target instead.
        QString resolve(const Value& value) const;
        QString label(quint32 offset) const;             // label-section string at an offset
        // The container's decoded table section (uncompressed header prefix + inflated body).
        const QByteArray& tables() const;
        // A big-endian float32 series over arbitrary entry bytes (the "array" view of an entry's tail).
        static QVector<float> floatsOf(const QByteArray& bytes);

        // Implementation detail: the file/table buffers and lazy caches live behind this shared block so
        // callers never see the container's storage layout and a Container can be handed out by reference.
        struct Data;
        std::shared_ptr<Data> data;
    };

    explicit RcoObjects(Assets& assets);

    // The parsed container, read on first request and cached. A missing/corrupt container comes back
    // with ok == false and error set; it is not retried.
    const Container& container(const QString& name);
    bool isLoaded(const QString& name) const;
    // Every vsh/resource/*.rco the firmware tree carries, base names sorted (no I/O into the files).
    QStringList containers() const;
    Assets& assets() const { return *assets_; }

    // Cost of the reads this instance performed, for the CLI's measurement line and the audit.
    struct Cost {
        int containers = 0;      // containers actually parsed
        qint64 bytesRead = 0;    // bytes of RCO files read
        qint64 nanoseconds = 0;  // read + inflate + walk, summed
    };
    Cost cost() const { return cost_; }
    void resetCost() { cost_ = Cost{}; }

    // The object types this reader knows (from the format schema), "0x802 Plane".
    static QStringList objectTypes();
    static QString schemaSource();

private:
    Assets* assets_ = nullptr;
    QHash<QString, std::shared_ptr<Container>> containers_;
    Cost cost_;
};

// P1.3: the firmware's material constants, read from the MNU/override files in the QGL archives through
// Assets::qglFile, plus the RCO Text object's own material. Nothing here is a fallback table: an absent
// key is absent (number() returns the caller's fallback), so a consumer can tell "the firmware does not
// set this" apart from "the firmware sets it to N".
class Materials final {
public:
    struct Item {
        enum class Kind { Number, Text } kind = Kind::Number;
        double number = 0;
        QString text;
    };
    // One .mnu file: "#MNU_1.0" then "NAME:type:value" lines, type float or int (text is kept verbatim
    // if a future preset carries another type).
    struct Mnu {
        QString archive, path, name;   // archive "lines"/"icons", path inside the QRC, base name
        bool ok = false;
        QString error;
        QVector<QPair<QString, Item>> entries;   // stored order
        QHash<QString, Item> byName;
        bool contains(const QString& key) const { return byName.contains(key); }
        double number(const QString& key, double fallback = 0) const;
        qint64 integer(const QString& key, qint64 fallback = 0) const;
        QString text(const QString& key, const QString& fallback = {}) const;
        QStringList keys() const;      // stored order
        QString source() const { return archive + ":" + path; }
    };

    explicit Materials(Assets& assets, RcoObjects& objects);

    // A file inside a QGL archive (archive "lines", "icons", ...), cached by (archive, path).
    const Mnu& mnu(const QString& archive, const QString& path);
    // The firmware's named presets. An empty preset reads the base file (ICONS.mnu, PARTICLES.mnu, ...);
    // "day"/"night"/"yoake"/"higure"/"coldboot1".. read override/<preset>/<NAME>.mnu.
    const Mnu& icons(const QString& preset = QStringLiteral("day"));
    const Mnu& particles(const QString& preset = {});
    const Mnu& particlesUi();
    const Mnu& particlesSpe();
    const Mnu& background(const QString& preset = {});
    const Mnu& line1(const QString& preset = {});
    const Mnu& hdr(const QString& preset = {});
    // Override preset names that carry <stem>.mnu inside a QRC archive (no file I/O beyond the archive).
    QStringList presets(const QString& archive, const QString& stem) const;

    // The firmware's text material: the fields of an RCO Text object (font reference, size, top/bottom
    // gradient colour, spacing and shadow). There is no TEXT.mnu in the local firmware (see
    // audit/RCO-OBJECT-MODEL.md 5.3); a Text object carries the material the plugin draws it with.
    struct TextMaterial {
        bool present = false;
        QString container, object;
        double size = 0;                 // font size (0 = the plugin's default)
        QString fontReference;           // resolved name/label of the font child, empty when unset
        QColor top, bottom;              // topRed..bottomBlue, 0..1 floats, alpha 1
        double spacingHorizontal = 0, spacingVertical = 0;
        QColor shadow;                   // shadowRed..shadowAlpha
        double shadowX = 0, shadowY = 0, shadowPerspective = 0;
    };
    TextMaterial textMaterial(const QString& container, const QString& object);

private:
    Assets* assets_ = nullptr;
    RcoObjects* objects_ = nullptr;
    QHash<QString, std::shared_ptr<Mnu>> mnus_;
    mutable QHash<QString, QStringList> presets_;   // archive -> every file name in it, on first use
};

} // namespace xmb
