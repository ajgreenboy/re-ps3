#pragma once
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {
// A PS3 theme (.p3t), read in memory. Ported from MaXMB (MIT, src-tauri/src/p3t.rs), itself adapted from DeckyXMB's
// p3t.py (BSD 3-Clause); see THIRD-PARTY.md. The file is a tree of named elements (theme > infotable / icontable /
// bgimagetable / setable ...) whose attributes are ints, floats, strings, ids or packed files; files are zlib-compressed
// and stored after the tree. Icons and previews are GIM images, still backgrounds JPEG or PNG, sound effects VAG.
// Dynamic themes also carry an animated RAF scene ("anim"); its container is parsed here as data, not drawn.
//
// What the reader exposes, and which parts the native UI consumes today, is measured in audit/THEMES.md.
class P3t {
public:
    struct Attribute {
        enum class Kind { Int, Float, String, Ints, Floats, File } kind=Kind::Int;
        qint64 integer=0; double real=0; QString text;
        QVector<qint64> integers; QVector<double> reals;
        quint32 offset=0,size=0; // File: packed bytes within the file table
    };
    struct Element { int depth=0; QString name; QHash<QString,Attribute> attributes; QString text(const QString& key) const; };

    // pointertable: one of the theme's mouse pointers and the hotspot inside its picture.
    struct Pointer { QImage image; int x=0,y=0; };

    // A dynamic theme's animated scene (bgimage/anim, a RAF) or a .raf opened directly, parsed as data only.
    // A RAF uses the same element tree as a .p3t and stores PS3 Edge models (.edge/.skel/.anim), GTF textures and
    // one VSMX script (compiled PlayStation JavaScript). The timeline is therefore split: animation headers carry
    // duration/fps/frames, while the motion itself is code in the script. Nothing in this struct renders.
    struct Raf {
        struct Section { quint32 offset=0,size=0; };
        struct Vec3 { double x=0,y=0,z=0; };
        struct Vec4 { double x=0,y=0,z=0,w=0; };
        struct Camera {
            bool present=false; QString id; int type=0;
            double yfov=0,ymag=0,znear=0,zfar=0;
            Vec3 position,direction,up;
        };
        struct Light { QString id; int type=0; Vec3 color,position,direction,attenuation; };
        struct Actor {
            QString id,model,material;
            Vec3 position,rotation,scale,uvScale,uvOffset;
            Vec4 color{1,1,1,1};
        };
        // An Edge .anim file header (EA03): its bounds are timeline metadata, not decoded keyframes.
        struct Animation {
            QString id,file; double start=0,end=0,duration=0,fps=0;
            int frames=0,joints=0; bool header=false;
        };
        struct Model {
            QString id,geometry,skeleton;
            quint32 geometryBytes=0,skeletonBytes=0;
            QVector<Animation> animations;
        };
        struct Texture { QString id,file,format; int type=0,width=0,height=0; quint32 bytes=0; };
        struct Material {
            QString id,effect,colorMap,normalMap,specularMap;
        };
        struct Script {
            bool present=false; QString id,file; quint32 bytes=0;
            bool vsmx=false; quint32 version=0,instructions=0,names=0;
        };

        bool valid=false;          // a RAF container was found and parsed
        bool embedded=false;       // came from a theme's bgimage/anim rather than a .raf file
        bool compressed=false;     // the outer container was "_RAF" + zlib
        QString container;         // "P3TF" / "RAFO" / "_RAF"
        quint32 version=0;         // the container header's first word (theme 272, RAF 1)
        QVector<Section> sections; // the six (offset,size) pairs after the magic
        int treeElements=0,treeAttributes=0;
        int sceneCount=0;
        Camera camera;
        QVector<Light> lights;
        QVector<Actor> actors;
        QVector<Model> models;
        QVector<Texture> textures;
        QVector<Material> materials;
        Script script;
        int animationCount=0;
        double timelineStart=0,timelineEnd=0; // min start / max end (or duration) over the animation headers
    };

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
    // The infotable fields the native UI does not show yet; empty when the theme leaves them out.
    QString comment() const;
    QString url() const;
    QString genre() const;
    QString infoVersion() const;
    // info/icon is the theme's own 64x64 picture, info/authoricon its author's; preview() stays the XMB screenshot.
    QImage themeIcon() const;
    QImage themeAuthorIcon() const;
    // localizedinfo: locale code -> that language's theme name (the XMB swaps them by language setting).
    QHash<QString,QString> localizedNames() const;
    // The theme's appearance selections (PSDevWiki P3T): font 0 original / 1 rounded / 2 pop; colour 0 follows the
    // month, 1..12 pins a month's palette. -1 when the theme has no such element.
    int fontSelection() const;
    QString fontSelectionName() const;
    int colorSelection() const;
    QString colorSelectionName() const;
    // pointertable (pointer pictures with their hotspots), the notification plaque, and every setable role.
    QHash<QString,Pointer> pointers() const;
    QImage notification() const;
    QStringList soundRoles() const; // every "se" id in tree order, not only the five the shell maps
    bool dynamic() const;          // has an animated scene (its still background, if any, is still used)
    QImage background() const;     // the first bgimage's hd picture, else its sd one
    // Every bgimage's hd (else sd) picture in tree order. Air Paint carries two; the rule that picks one is open.
    QVector<QImage> backgrounds() const;
    QImage preview() const;        // info/preview: a screenshot of the theme
    QHash<QString,QImage> icons() const;        // icontable: PS3 icon id ("icon_game", ...) -> picture
    QHash<QString,QByteArray> sounds() const;   // setable: sound id ("snd_cursor", ...) -> 16-bit PCM WAV
    // Opaque container facts, plus the parsed RAF scene (invalid for a theme without an animated background).
    QString container() const { return container_; }
    quint32 containerVersion() const { return containerVersion_; }
    bool hasScene() const { return scene_.valid; }
    const Raf& scene() const { return scene_; }
    // The first image of a GIM, decoded by the same reader RCO images use (RGBA8888, DXT1/3/5, indexed with
    // a palette, and the palette-less indexed coverage masks a user theme may carry). Null when not a GIM.
    static QImage gim(const QByteArray& data);
    // PlayStation ADPCM (VAG, 28 samples per 16-byte block) as a WAV; with right, the two make a stereo sound.
    static QByteArray vagToWav(const QByteArray& left,const QByteArray& right={});
private:
    static P3t readContainer(QByteArray data,QString* error,int nesting);
    void readScene(int depth);
    void trace() const;
    QString infoText(const QString& key) const;
    int selection(const QString& element) const;
    QByteArray data_,files_;
    QVector<Element> elements_;
    QString container_="P3TF";
    quint32 containerVersion_=0;
    bool sceneCompressed_=false;
    QVector<Raf::Section> sections_;
    Raf scene_;
};
}
