#pragma once
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

namespace xmb {
// A PS3 theme (.p3t), read in memory. Ported from MaXMB (MIT, src-tauri/src/p3t.rs), itself adapted from DeckyXMB's
// p3t.py (BSD 3-Clause); see THIRD-PARTY.md. The file is a tree of named elements (theme > infotable / icontable /
// bgimagetable / setable ...) whose attributes are ints, floats, strings, ids or packed files; files are zlib-compressed
// and stored after the tree. Icons and previews are GIM images, still backgrounds JPEG or PNG, sound effects VAG.
// Dynamic themes also carry an animated RAF scene ("anim"), which is listed but not drawn here.
class P3t {
public:
    struct Attribute {
        enum class Kind { Int, Float, String, Ints, Floats, File } kind=Kind::Int;
        qint64 integer=0; double real=0; QString text;
        QVector<qint64> integers; QVector<double> reals;
        quint32 offset=0,size=0; // File: packed bytes within the file table
    };
    struct Element { int depth=0; QString name; QHash<QString,Attribute> attributes; QString text(const QString& key) const; };
    static P3t read(QByteArray data,QString* error=nullptr);
    static P3t load(const QString& path,QString* error=nullptr);
    bool isNull() const { return elements_.isEmpty(); }
    // Every element in tree order (depth gives the nesting).
    const QVector<Element>& elements() const { return elements_; }
    // A packed file's bytes, inflated (stored as they are when they are not zlib data); empty for other kinds.
    QByteArray file(const Attribute& attribute) const;
    QByteArray file(const Element& element,const QString& key) const;
    const Element* find(const QString& name) const;
    QString name() const;
    QString author() const;
    bool dynamic() const;          // has an animated scene (its still background, if any, is still used)
    QImage background() const;     // the first bgimage's hd picture, else its sd one
    QImage preview() const;        // info/preview: a screenshot of the theme
    QHash<QString,QImage> icons() const;        // icontable: PS3 icon id ("icon_game", ...) -> picture
    QHash<QString,QByteArray> sounds() const;   // setable: sound id ("snd_cursor", ...) -> 16-bit PCM WAV
    // The first image of a GIM (direct colour, 4/8-bit palettes, swizzled or not).
    static QImage gim(const QByteArray& data);
    // PlayStation ADPCM (VAG, 28 samples per 16-byte block) as a WAV; with right, the two make a stereo sound.
    static QByteArray vagToWav(const QByteArray& left,const QByteArray& right={});
private:
    QByteArray data_,files_;
    QVector<Element> elements_;
};
}
