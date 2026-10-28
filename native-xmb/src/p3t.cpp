#include "p3t.h"
#include "assets.h"
#include <QDebug>
#include <QFile>
#include <QSet>
#include <QtEndian>
#include <bit>
#include <algorithm>
#include <cmath>
#include <utility>

namespace xmb {
namespace {
constexpr quint32 kNone=0xFFFFFFFFu;
quint32 be32(const QByteArray& d,qsizetype at){return at>=0 && at+4<=d.size()?qFromBigEndian<quint32>(d.constData()+at):kNone;}
quint16 be16(const QByteArray& d,qsizetype at){return at>=0 && at+2<=d.size()?qFromBigEndian<quint16>(d.constData()+at):0;}
quint32 le32(const QByteArray& d,qsizetype at){return at>=0 && at+4<=d.size()?qFromLittleEndian<quint32>(d.constData()+at):0;}
// Python-style slice: stops at the end of the data.
QByteArray clip(const QByteArray& d,qsizetype a,qsizetype b){a=std::clamp<qsizetype>(a,0,d.size());return d.mid(a,std::clamp<qsizetype>(b,a,d.size())-a);}
// Attribute readers shared by the theme selectors and the RAF scene (ints and floats are both accepted where the
// XML allows either).
double number(const P3t::Element& element,const QString& key,double fallback=0) {
    const auto found=element.attributes.constFind(key);
    if(found==element.attributes.cend())return fallback;
    if(found->kind==P3t::Attribute::Kind::Int)return double(found->integer);
    if(found->kind==P3t::Attribute::Kind::Float)return found->real;
    return fallback;
}
P3t::Raf::Vec3 vector3(const P3t::Element& element,const QString& key) {
    P3t::Raf::Vec3 value;
    const auto found=element.attributes.constFind(key);
    if(found==element.attributes.cend())return value;
    if(found->kind==P3t::Attribute::Kind::Floats) {
        if(found->reals.size()>0)value.x=found->reals[0];
        if(found->reals.size()>1)value.y=found->reals[1];
        if(found->reals.size()>2)value.z=found->reals[2];
    } else if(found->kind==P3t::Attribute::Kind::Float)value.x=found->real;
    return value;
}
P3t::Raf::Vec4 vector4(const P3t::Element& element,const QString& key) {
    P3t::Raf::Vec4 value{0,0,0,0};
    const auto found=element.attributes.constFind(key);
    if(found==element.attributes.cend())return value;
    if(found->kind==P3t::Attribute::Kind::Floats) {
        if(found->reals.size()>0)value.x=found->reals[0];
        if(found->reals.size()>1)value.y=found->reals[1];
        if(found->reals.size()>2)value.z=found->reals[2];
        if(found->reals.size()>3)value.w=found->reals[3];
    } else if(found->kind==P3t::Attribute::Kind::Float)value.x=found->real;
    return value;
}
QString monthName(int month) {
    static const char* names[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    return month>=1 && month<=12?QString::fromLatin1(names[month-1]):QString{};
}
// GTF: one header, then per texture (id, offset, size) and a CellGcmTexture (format at +0x18, mips +0x19,
// width +0x20, height +0x22, pitch +0x28). Only the first texture's metadata is reported.
QString gtfFormatName(int format) {
    const int base=format & ~0x60;
    if(base==0x85)return QStringLiteral("RGBA8");
    if(base==0x86)return QStringLiteral("DXT1");
    if(base==0x87)return QStringLiteral("DXT3");
    if(base==0x88)return QStringLiteral("DXT5");
    if(base==0x81)return QStringLiteral("B8"); // 0xA1 with the linear bit: coldboot's logo textures
    return QStringLiteral("0x%1").arg(format,2,16,QChar('0'));
}
bool gtfInfo(const QByteArray& gtf,int& width,int& height,QString& format) {
    if(gtf.size()<0x30 || be32(gtf,8)<1)return false;
    format=gtfFormatName(quint8(gtf.at(0x18)));
    width=be16(gtf,0x20);height=be16(gtf,0x22);
    return width>0 && height>0;
}
QString cstr(const QByteArray& d,qsizetype at) {
    if(at<0 || at>=d.size())return {};
    const qsizetype end=d.indexOf('\0',at);
    return QString::fromUtf8(d.constData()+at,(end<0?d.size():end)-at);
}
// zlib stream without the size header qUncompress expects; the expanded size is not stored, so grow the hint.
QByteArray inflate(const QByteArray& packed) {
    if(packed.size()<2 || (quint8(packed[0])&0x0F)!=8 || ((quint8(packed[0])<<8)|quint8(packed[1]))%31)return packed;
    for(quint64 hint=std::max<quint64>(quint64(packed.size())*4,1<<16);hint<=(256u<<20);hint*=4) {
        QByteArray framed(4,'\0');qToBigEndian<quint32>(quint32(hint),framed.data());framed+=packed;
        auto out=qUncompress(framed);
        if(!out.isEmpty())return out;
    }
    return packed;
}
}

QString P3t::Element::text(const QString& key) const {
    const auto found=attributes.constFind(key);
    return found!=attributes.cend() && found->kind==Attribute::Kind::String?found->text:QString{};
}

P3t P3t::read(QByteArray data,QString* error) { return readContainer(std::move(data),error,0); }

P3t P3t::readContainer(QByteArray data,QString* error,int nesting) {
    const auto fail=[&](const QString& why){if(error)*error=why;return P3t{};};
    // A .p3t is "P3TF"; a RAF scene is "RAFO", either raw or inside a "_RAF" + u32 size + zlib container (the same
    // wrapper coldboot.raf uses). Some tools store a bare zlib stream, which the fallback unpacker accepts.
    QByteArray raw;QString container;bool compressedScene=false;
    if(data.startsWith("P3TF")) {raw=std::move(data);container=QStringLiteral("P3TF");}
    else if(data.startsWith("RAFO")) {raw=std::move(data);container=QStringLiteral("RAFO");}
    else if(data.startsWith("_RAF")) {
        const quint32 declared=be32(data,4);
        if(declared==kNone || declared>256u*1024*1024)return fail("The RAF scene is too large to read.");
        raw=qUncompress(data.mid(4));
        if(!raw.startsWith("RAFO"))return fail("The RAF scene could not be decompressed.");
        container=QStringLiteral("_RAF");compressedScene=true;
    } else {
        const auto unpacked=inflate(data);
        if(!unpacked.startsWith("RAFO"))return fail("This is not a PS3 theme file.");
        raw=unpacked;container=QStringLiteral("RAFO");compressedScene=true;
    }
    if(raw.size()<56)return fail("This is not a PS3 theme file.");
    quint32 h[13];for(int i=0;i<13;++i)h[i]=be32(raw,4+4*i);
    P3t theme;
    const QByteArray tree=clip(raw,h[1],qsizetype(h[1])+h[2]),ids=clip(raw,h[3],qsizetype(h[3])+h[4]),strings=clip(raw,h[5],qsizetype(h[5])+h[6]);
    const QByteArray intBytes=clip(raw,h[7],qsizetype(h[7])+h[8]-h[8]%4),floatBytes=clip(raw,h[9],qsizetype(h[9])+h[10]-h[10]%4);
    theme.files_=clip(raw,h[11],qsizetype(h[11])+h[12]);
    QVector<qint64> ints;QVector<double> floats;
    for(qsizetype i=0;i+4<=intBytes.size();i+=4)ints.push_back(qint32(be32(intBytes,i)));
    for(qsizetype i=0;i+4<=floatBytes.size();i+=4)floats.push_back(std::bit_cast<float>(be32(floatBytes,i)));
    // Element: name, attribute count, parent, previous, next sibling, first child, last child (28 bytes), then
    // 16-byte attributes {name, kind, value, value}. Walked depth-first; a node seen twice ends that branch.
    QVector<QPair<quint32,int>> stack{{0,0}};QSet<quint32> seen;
    while(!stack.isEmpty()) {
        const auto [off,depth]=stack.takeLast();
        if(qsizetype(off)+28>tree.size() || seen.contains(off) || seen.size()>200000)continue;
        seen.insert(off);
        const auto w=[&](int i){return be32(tree,qsizetype(off)+4*i);};
        Element element{depth,cstr(strings,w(0)),{}};
        for(quint32 i=0;i<w(1);++i) {
            const qsizetype a=qsizetype(off)+28+16*qsizetype(i);
            if(a+16>tree.size())break;
            const quint32 kind=be32(tree,a+4),v1=be32(tree,a+8),v2=be32(tree,a+12);
            Attribute value;
            const auto range=[&](qsizetype n){return std::pair{std::min<qsizetype>(v1,n),std::min<qsizetype>(qsizetype(v1)+v2,n)};};
            switch(kind) {
            case 1:value.kind=Attribute::Kind::Int;value.integer=qint32(v1);break;
            case 2:value.kind=Attribute::Kind::Float;value.real=std::bit_cast<float>(v1);break;
            case 3:value.kind=Attribute::Kind::String;value.text=cstr(strings,v1);break;
            case 4:{value.kind=Attribute::Kind::Ints;const auto [b,e]=range(ints.size());value.integers=ints.mid(b,e-b);break;}
            case 5:{value.kind=Attribute::Kind::Floats;const auto [b,e]=range(floats.size());value.reals=floats.mid(b,e-b);break;}
            case 6:value.kind=Attribute::Kind::File;value.offset=v1;value.size=v2;break;
            case 7:case 8:value.kind=Attribute::Kind::String;value.text=cstr(ids,qsizetype(v1)+4);break; // after a 4-byte back reference
            default:continue;
            }
            element.attributes.insert(cstr(strings,be32(tree,a)),std::move(value));
        }
        theme.elements_.push_back(std::move(element));
        if(w(4)!=kNone)stack.push_back({w(4),depth});
        if(w(5)!=kNone)stack.push_back({w(5),depth+1});
    }
    if(theme.elements_.isEmpty())return fail("The theme file holds no elements.");
    theme.data_=std::move(raw);
    theme.container_=container;
    theme.containerVersion_=h[0];
    theme.sceneCompressed_=compressedScene;
    theme.sections_.clear();
    for(int i=0;i<6;++i)theme.sections_.push_back(Raf::Section{h[1+2*i],h[2+2*i]});
    theme.readScene(nesting);
    if(nesting==0)theme.trace();
    return theme;
}
P3t P3t::load(const QString& path,QString* error) {
    QFile file(path);
    // Retail themes are a few MB to ~100 MB; the whole file stays in memory while the theme is read.
    if(!file.open(QIODevice::ReadOnly) || file.size()>512ll*1024*1024){if(error)*error="The theme file could not be read.";return {};}
    return read(file.readAll(),error);
}
QByteArray P3t::file(const Attribute& attribute) const {
    if(attribute.kind!=Attribute::Kind::File)return {};
    return inflate(clip(files_,attribute.offset,qsizetype(attribute.offset)+attribute.size));
}
QByteArray P3t::file(const Element& element,const QString& key) const {
    const auto found=element.attributes.constFind(key);
    return found==element.attributes.cend()?QByteArray{}:file(*found);
}
const P3t::Element* P3t::find(const QString& name) const {
    for(const auto& element:elements_)if(element.name==name)return &element;
    return nullptr;
}
QString P3t::name() const {const auto* info=find("info");return info?info->text("name"):QString{};}
QString P3t::author() const {const auto* info=find("info");return info?info->text("author"):QString{};}
QString P3t::infoText(const QString& key) const {const auto* info=find("info");return info?info->text(key):QString{};}
QString P3t::comment() const {return infoText(QStringLiteral("comment"));}
QString P3t::url() const {return infoText(QStringLiteral("url"));}
QString P3t::genre() const {return infoText(QStringLiteral("genre"));}
QString P3t::infoVersion() const {return infoText(QStringLiteral("version"));}
QImage P3t::themeIcon() const {const auto* info=find("info");return info?gim(file(*info,QStringLiteral("icon"))):QImage{};}
QImage P3t::themeAuthorIcon() const {const auto* info=find("info");return info?gim(file(*info,QStringLiteral("authoricon"))):QImage{};}
QHash<QString,QString> P3t::localizedNames() const {
    QHash<QString,QString> result;
    for(const auto& element:elements_) {
        if(element.name!="localizedinfo")continue;
        const auto locale=element.text(QStringLiteral("locale")),translated=element.text(QStringLiteral("name"));
        if(!locale.isEmpty() && !translated.isEmpty() && !result.contains(locale))result.insert(locale,translated);
    }
    return result;
}
int P3t::selection(const QString& element) const {
    const auto* found=find(element);
    if(!found)return -1;
    const auto value=found->attributes.constFind(QStringLiteral("selection"));
    if(value==found->attributes.cend())return -1;
    if(value->kind==Attribute::Kind::Int)return int(value->integer);
    if(value->kind==Attribute::Kind::Float)return int(value->real);
    return -1;
}
int P3t::fontSelection() const {return selection(QStringLiteral("font"));}
QString P3t::fontSelectionName() const {
    switch(fontSelection()) {
    case 0:return QStringLiteral("Original");
    case 1:return QStringLiteral("Rounded");
    case 2:return QStringLiteral("Pop");
    case -1:return QString{};
    default:return QStringLiteral("Font %1").arg(fontSelection());
    }
}
int P3t::colorSelection() const {return selection(QStringLiteral("color"));}
QString P3t::colorSelectionName() const {
    const int value=colorSelection();
    if(value==0)return QStringLiteral("Original");
    if(value>=1 && value<=12)return monthName(value);
    return value<0?QString{}:QStringLiteral("Color %1").arg(value);
}
QHash<QString,P3t::Pointer> P3t::pointers() const {
    QHash<QString,Pointer> result;
    for(const auto& element:elements_) {
        if(element.name!="pointer")continue;
        const auto id=element.text(QStringLiteral("id"));
        if(id.isEmpty() || result.contains(id))continue;
        Pointer pointer;
        pointer.x=int(number(element,QStringLiteral("base_x")));
        pointer.y=int(number(element,QStringLiteral("base_y")));
        pointer.image=gim(file(element,QStringLiteral("src")));
        result.insert(id,pointer);
    }
    return result;
}
QImage P3t::notification() const {
    for(const auto& element:elements_)if(element.name=="notification")return gim(file(element,QStringLiteral("src")));
    return {};
}
QStringList P3t::soundRoles() const {
    QStringList roles;
    for(const auto& element:elements_) {
        if(element.name!="se")continue;
        const auto id=element.text(QStringLiteral("id"));
        if(!id.isEmpty() && !roles.contains(id))roles<<id;
    }
    return roles;
}
bool P3t::dynamic() const {
    for(const auto& element:elements_)if(element.name=="bgimage" && element.attributes.contains("anim"))return true;
    return false;
}
QImage P3t::background() const {
    for(const auto& element:elements_) {
        if(element.name!="bgimage")continue;
        for(const QString& key:{QStringLiteral("hd"),QStringLiteral("sd")}) {
            const auto bytes=file(element,key);
            if(bytes.isEmpty())continue;
            auto image=QImage::fromData(bytes);
            if(!image.isNull())return image;
        }
    }
    return {};
}
QVector<QImage> P3t::backgrounds() const {
    QVector<QImage> result;
    for(const auto& element:elements_) {
        if(element.name!="bgimage")continue;
        for(const QString& key:{QStringLiteral("hd"),QStringLiteral("sd")}) {
            const auto bytes=file(element,key);
            if(bytes.isEmpty())continue;
            auto image=QImage::fromData(bytes);
            if(image.isNull())continue;
            result.push_back(std::move(image));
            break; // one picture per bgimage: hd when present, else sd
        }
    }
    return result;
}
QImage P3t::preview() const {const auto* info=find("info");return info?gim(file(*info,"preview")):QImage{};}
QHash<QString,QImage> P3t::icons() const {
    QHash<QString,QImage> result;
    for(const auto& element:elements_) {
        if(element.name!="icon")continue;
        const auto id=element.text("id");
        if(id.isEmpty() || result.contains(id))continue;
        auto image=gim(file(element,"src"));
        if(!image.isNull())result.insert(id,std::move(image)); // an icon that can't be read keeps the system's
    }
    return result;
}
QHash<QString,QByteArray> P3t::sounds() const {
    QHash<QString,QByteArray> result;
    for(const auto& element:elements_) {
        if(element.name!="se")continue;
        const auto id=element.text("id");
        if(id.isEmpty() || result.contains(id))continue;
        const auto mono=file(element,"src"),left=file(element,"left"),right=file(element,"right");
        auto wav=!mono.isEmpty()?vagToWav(mono):!left.isEmpty()?vagToWav(left,right):QByteArray{};
        if(!wav.isEmpty())result.insert(id,std::move(wav));
    }
    return result;
}

// A dynamic theme keeps its RAF in bgimage/anim; a .raf opened directly is already the scene tree. The element tree
// is walked once here: actors, one camera, up to two lights, Edge models with animation headers, GTF texture headers,
// material map bindings and the VSMX script header. Keyframes and scripts are not interpreted (audit/THEMES.md).
void P3t::readScene(int depth) {
    scene_=Raf{};
    if(container_=="P3TF") {
        if(depth>=2)return; // one theme layer + one scene layer; a nested anim is not a real theme structure
        for(const auto& element:elements_) {
            if(element.name!="bgimage")continue;
            const auto anim=file(element,QStringLiteral("anim"));
            if(anim.isEmpty())continue;
            QString error;
            const auto inner=readContainer(anim,&error,depth+1);
            if(inner.hasScene()) {
                scene_=inner.scene();
                scene_.embedded=true;
                return;
            }
        }
        return;
    }
    Raf& scene=scene_;
    scene.valid=true;
    scene.container=container_;
    scene.compressed=sceneCompressed_;
    scene.version=containerVersion_;
    scene.sections=sections_;
    scene.treeElements=elements_.size();
    QHash<QString,const Element*> files;
    for(const auto& element:elements_) {
        scene.treeAttributes+=element.attributes.size();
        if(element.name=="scene")++scene.sceneCount;
        if(element.name=="file") {
            const auto id=element.text(QStringLiteral("id"));
            if(!id.isEmpty() && element.attributes.contains(QStringLiteral("src")))files.insert(id,&element);
        }
    }
    const auto fileElement=[&](const QString& id){return files.value(id,nullptr);};
    const auto fileBytes=[&](const QString& id) {
        const auto* found=fileElement(id);
        return found?file(*found,QStringLiteral("src")):QByteArray{};
    };
    const auto fileSize=[&](const QString& id) {
        const auto* found=fileElement(id);
        if(!found)return quint32(0);
        const auto src=found->attributes.constFind(QStringLiteral("src"));
        return src!=found->attributes.cend() && src->kind==Attribute::Kind::File?src->size:quint32(0);
    };
    // Textures first: materials name them by id.
    QHash<QString,int> textureTypes;
    for(const auto& element:elements_) {
        if(element.name!="texture")continue;
        Raf::Texture texture;
        texture.id=element.text(QStringLiteral("id"));
        texture.type=int(number(element,QStringLiteral("type")));
        texture.file=element.text(QStringLiteral("fileref"));
        const auto bytes=fileBytes(texture.file);
        texture.bytes=quint32(bytes.size());
        int width=0,height=0;QString format;
        if(gtfInfo(bytes,width,height,format)){texture.width=width;texture.height=height;texture.format=format;}
        textureTypes.insert(texture.id,texture.type);
        scene.textures.push_back(texture);
    }
    QString materialId;
    for(const auto& element:elements_) {
        if(element.name=="material") {
            Raf::Material material;
            material.id=element.text(QStringLiteral("id"));
            material.effect=element.text(QStringLiteral("effect"));
            scene.materials.push_back(material);
            materialId=material.id;
        } else if(element.name=="_texture" && !materialId.isEmpty()) {
            const auto reference=element.text(QStringLiteral("texref"));
            const int type=textureTypes.value(reference,-1);
            for(auto& material:scene.materials) {
                if(material.id!=materialId)continue;
                if(type==0 && material.colorMap.isEmpty())material.colorMap=reference;
                else if((type==1 || type==3) && material.normalMap.isEmpty())material.normalMap=reference;
                else if(type==2 && material.specularMap.isEmpty())material.specularMap=reference;
                break;
            }
        }
    }
    for(const auto& element:elements_) {
        if(element.name=="camera" && !scene.camera.present) {
            auto& camera=scene.camera;
            camera.present=true;camera.id=element.text(QStringLiteral("id"));
            camera.type=int(number(element,QStringLiteral("type")));
            camera.yfov=number(element,QStringLiteral("yfov"));
            camera.ymag=number(element,QStringLiteral("ymag"));
            camera.znear=number(element,QStringLiteral("znear"),0.1);
            camera.zfar=number(element,QStringLiteral("zfar"),1000);
            camera.position=vector3(element,QStringLiteral("position"));
            camera.direction=vector3(element,QStringLiteral("direction"));
            camera.up=vector3(element,QStringLiteral("up"));
        } else if(element.name=="light") {
            Raf::Light light;
            light.id=element.text(QStringLiteral("id"));
            light.type=int(number(element,QStringLiteral("type")));
            light.color=vector3(element,QStringLiteral("color"));
            light.position=vector3(element,QStringLiteral("position"));
            light.direction=vector3(element,QStringLiteral("direction"));
            light.attenuation=vector3(element,QStringLiteral("attenuation"));
            scene.lights.push_back(light);
        } else if(element.name=="actor") {
            Raf::Actor actor;
            actor.id=element.text(QStringLiteral("id"));
            actor.model=element.text(QStringLiteral("model"));
            actor.material=element.text(QStringLiteral("material"));
            actor.position=vector3(element,QStringLiteral("position"));
            actor.rotation=vector3(element,QStringLiteral("rotation"));
            actor.scale=vector3(element,QStringLiteral("scale"));
            actor.uvScale=vector3(element,QStringLiteral("uv_scale"));
            actor.uvOffset=vector3(element,QStringLiteral("uv_offset"));
            if(element.attributes.contains(QStringLiteral("color")))actor.color=vector4(element,QStringLiteral("color"));
            scene.actors.push_back(actor);
        }
    }
    // A model's geometry/skeleton/animation are its direct children; the flat tree keeps depth, so the children are
    // the following elements with depth+1. Several animations per model are blended by the script.
    for(qsizetype i=0;i<elements_.size();++i) {
        const auto& element=elements_[i];
        if(element.name!="model")continue;
        Raf::Model model;
        model.id=element.text(QStringLiteral("id"));
        for(qsizetype j=i+1;j<elements_.size() && elements_[j].depth>element.depth;++j) {
            const auto& child=elements_[j];
            if(child.depth!=element.depth+1)continue;
            if(child.name=="geometry") {model.geometry=child.text(QStringLiteral("fileref"));model.geometryBytes=fileSize(model.geometry);}
            else if(child.name=="skeleton") {model.skeleton=child.text(QStringLiteral("fileref"));model.skeletonBytes=fileSize(model.skeleton);}
            else if(child.name=="animation") {
                Raf::Animation animation;
                animation.id=child.text(QStringLiteral("id"));
                animation.file=child.text(QStringLiteral("fileref"));
                animation.start=number(child,QStringLiteral("start"));
                animation.end=number(child,QStringLiteral("end"));
                const auto bytes=fileBytes(animation.file);
                if(bytes.startsWith("EA03") && bytes.size()>=20) {
                    animation.header=true;
                    animation.duration=double(std::bit_cast<float>(be32(bytes,4)));
                    animation.fps=double(std::bit_cast<float>(be32(bytes,8)));
                    animation.joints=be16(bytes,14);
                    animation.frames=be16(bytes,16);
                }
                model.animations.push_back(animation);
            }
        }
        scene.models.push_back(model);
    }
    for(const auto& model:scene.models)for(const auto& animation:model.animations) {
        if(animation.start==0 && animation.end==0 && animation.duration==0 && !animation.header)continue;
        const double end=animation.end>0?animation.end:animation.duration;
        if(++scene.animationCount==1){scene.timelineStart=animation.start;scene.timelineEnd=end;}
        else {scene.timelineStart=std::min(scene.timelineStart,animation.start);scene.timelineEnd=std::max(scene.timelineEnd,end);}
    }
    for(const auto& element:elements_) {
        if(element.name!="script")continue;
        Raf::Script script;
        script.present=true;
        script.id=element.text(QStringLiteral("id"));
        script.file=element.text(QStringLiteral("fileref"));
        const auto bytes=fileBytes(script.file);
        script.bytes=quint32(bytes.size());
        if(bytes.startsWith("VSMX") && bytes.size()>=0x30) {
            script.vsmx=true;
            script.version=le32(bytes,4);
            script.instructions=le32(bytes,0xc)/8;
            const quint32 at=le32(bytes,0x28),size=le32(bytes,0x2c);
            if(at<quint32(bytes.size())) {
                const qsizetype end=std::min<qsizetype>(bytes.size(),qsizetype(at)+size);
                bool inName=false;quint32 names=0;
                for(qsizetype i=at;i<end;++i) {
                    if(bytes[i])inName=true;
                    else if(inName){++names;inName=false;}
                }
                script.names=names;
            }
        }
        scene.script=script;
        break;
    }
}

// XMB_P3T_TRACE=1 prints the parsed theme/scene facts through the existing --p3t-dump path. It is a diagnostic:
// normal runs never read it, and it changes no pixel.
void P3t::trace() const {
    if(!qEnvironmentVariableIsSet("XMB_P3T_TRACE"))return;
    QHash<QString,int> counts;int attributes=0;
    for(const auto& element:elements_){++counts[element.name];attributes+=element.attributes.size();}
    QStringList names=counts.keys();names.sort();
    QStringList tree;for(const auto& name:names)tree<<QStringLiteral("%1=%2").arg(name).arg(counts.value(name));
    const auto localized=localizedNames();
    QStringList locales=localized.keys();locales.sort();
    const auto themeIconImage=themeIcon(),authorIconImage=themeAuthorIcon(),previewImage=preview();
    qInfo().noquote()<<"p3t-trace: container"<<container_<<"version"<<containerVersion_<<"elements"<<elements_.size()
        <<"attributes"<<attributes<<"tree"<<QStringLiteral("[")+tree.join(',')+QStringLiteral("]");
    qInfo().noquote()<<"p3t-trace: info name"<<name()<<"author"<<author()<<"comment"<<comment()<<"url"<<url()
        <<"genre"<<genre()<<"infoVersion"<<infoVersion()
        <<"localized"<<localized.size()<<QStringLiteral("[")+locales.join(',')+QStringLiteral("]")
        <<"themeIcon"<<QStringLiteral("%1x%2").arg(themeIconImage.width()).arg(themeIconImage.height())
        <<"authorIcon"<<QStringLiteral("%1x%2").arg(authorIconImage.width()).arg(authorIconImage.height())
        <<"preview"<<QStringLiteral("%1x%2").arg(previewImage.width()).arg(previewImage.height());
    const auto pointerMap=pointers();QStringList pointerList;
    for(auto it=pointerMap.cbegin();it!=pointerMap.cend();++it)
        pointerList<<QStringLiteral("%1@%2,%3(%4x%5)").arg(it.key()).arg(it.value().x).arg(it.value().y)
            .arg(it.value().image.width()).arg(it.value().image.height());
    pointerList.sort();
    const auto note=notification();
    qInfo().noquote()<<"p3t-trace: font"<<fontSelection()<<QStringLiteral("(")+fontSelectionName()+QStringLiteral(")")
        <<"color"<<colorSelection()<<QStringLiteral("(")+colorSelectionName()+QStringLiteral(")")
        <<"roles"<<QStringLiteral("[")+soundRoles().join(',')+QStringLiteral("]")
        <<"pointers"<<QStringLiteral("[")+pointerList.join(',')+QStringLiteral("]")
        <<"notification"<<QStringLiteral("%1x%2").arg(note.width()).arg(note.height());
    if(!scene_.valid) {qInfo().noquote()<<"p3t-trace: scene none";return;}
    const auto& scene=scene_;
    QStringList actorList;for(const auto& actor:scene.actors)actorList<<actor.id;
    QStringList animationList;
    for(const auto& model:scene.models)for(const auto& animation:model.animations)
        animationList<<QStringLiteral("%1[%2..%3 %4fps %5fr]").arg(animation.id).arg(animation.start).arg(animation.end)
            .arg(animation.fps).arg(animation.frames);
    const QString camera=scene.camera.present?scene.camera.id:QStringLiteral("none");
    const QString script=scene.script.present?scene.script.file:QStringLiteral("none");
    qInfo().noquote()<<"p3t-trace: scene container"<<scene.container<<"version"<<scene.version
        <<"embedded"<<scene.embedded<<"compressed"<<scene.compressed<<"sections"<<scene.sections.size()
        <<"tree"<<scene.treeElements<<"/"<<scene.treeAttributes<<"scene"<<scene.sceneCount
        <<"actors"<<scene.actors.size()<<QStringLiteral("[")+actorList.join(',')+QStringLiteral("]")
        <<"models"<<scene.models.size()<<"materials"<<scene.materials.size()
        <<"textures"<<scene.textures.size()<<"lights"<<scene.lights.size()<<"camera"<<camera
        <<"animations"<<scene.animationCount<<QStringLiteral("[")+animationList.join(',')+QStringLiteral("]")
        <<"timeline"<<QStringLiteral("%1..%2").arg(scene.timelineStart).arg(scene.timelineEnd)
        <<"script"<<script<<"vsmx"<<scene.script.vsmx
        <<"instructions"<<scene.script.instructions<<"names"<<scene.script.names;
}

// One GIM decoder for the whole application. Theme pictures used to go through a private parser here, which
// returned a null image whenever an indexed picture (format 0x4/0x5/0x6) had no palette block - the same
// palette-less form the shared decoder reads as an 8-bit alpha coverage mask. A user theme whose icon, pointer
// or notification is such a mask therefore rendered nothing even though the RCO path could read it. Delegating
// keeps one reader for every GIM in the process: RGBA8888, DXT1/3/5 and the indexed formats with or without a
// palette. A payload that is not a GIM fails the shared decoder's byte-order check, so it still yields null;
// little-endian ("MIG.") PSP GIMs are not supported by the PS3 decoder and no local theme carries one
// (audit/THEMES.md section 8).
QImage P3t::gim(const QByteArray& g) {
    if(g.isEmpty())return {};
    return Assets::decodeGimImage(g);
}

QByteArray P3t::vagToWav(const QByteArray& left,const QByteArray& right) {
    const auto decode=[](const QByteArray& v,quint32& rate) {
        qsizetype p=0;rate=44100;
        if(v.startsWith("VAGp")){const quint32 r=be32(v,16);if(r && r!=kNone)rate=r;p=48;}
        static constexpr double k[5][2]{{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
        double s1=0,s2=0;QVector<qint16> out;
        for(;p+16<=v.size();p+=16) {
            const auto* b=reinterpret_cast<const uchar*>(v.constData()+p);
            if(b[1]==7)break; // end of the sound
            const int shift=std::min(b[0]&15,12);const auto& c=k[std::min(b[0]>>4,4)];
            for(int i=0;i<28;++i) {
                const int nibble=i%2?b[2+i/2]>>4:b[2+i/2]&15;
                const int raw=qint16(quint16(nibble<<12))>>4; // sign-extended, scaled to 12 bits
                const double x=std::clamp(std::round(double((raw*16)>>shift)+(s1*c[0]+s2*c[1])/64.),-32768.,32767.);
                out.push_back(qint16(x));s2=s1;s1=x;
            }
            if(b[1]==1)break; // last block, no loop
        }
        return out;
    };
    quint32 rate=44100,unused=0;
    const auto l=decode(left,rate);
    if(l.isEmpty())return {};
    const auto r=right.isEmpty()?QVector<qint16>{}:decode(right,unused);
    const quint16 channels=r.isEmpty()?1:2;
    const qsizetype frames=r.isEmpty()?l.size():std::min(l.size(),r.size());
    const quint32 bytes=quint32(frames)*channels*2;
    QByteArray w;w.reserve(44+bytes);
    const auto u32=[&](quint32 v){char b[4];qToLittleEndian(v,b);w.append(b,4);};
    const auto u16=[&](quint16 v){char b[2];qToLittleEndian(v,b);w.append(b,2);};
    w+="RIFF";u32(36+bytes);w+="WAVEfmt ";u32(16);u16(1);u16(channels);u32(rate);u32(rate*channels*2);u16(channels*2);u16(16);
    w+="data";u32(bytes);
    for(qsizetype i=0;i<frames;++i){u16(quint16(l[i]));if(channels==2)u16(quint16(r[i]));}
    return w;
}
}
