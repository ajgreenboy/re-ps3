#include "glyphs.h"
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <functional>

namespace xmb::glyphs {
namespace {
QImage canvas() { QImage image(256,256,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);return image; }
QImage line(const std::function<void(QPainter&)>& draw) {
    QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::NoBrush);
    QPen shadow(QColor(0,0,0,90));shadow.setWidthF(14);shadow.setCapStyle(Qt::RoundCap);shadow.setJoinStyle(Qt::RoundJoin);
    p.save();p.translate(3,5);p.setPen(shadow);draw(p);p.restore();
    QPen pen(QColor(250,250,250));pen.setWidthF(12);pen.setCapStyle(Qt::RoundCap);pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);draw(p);
    return image;
}
}

const QImage& spotifyLogo() {
    static const QImage logo=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        const double s=256.;
        p.setPen(Qt::NoPen);p.setBrush(QColor(0x1e,0xd7,0x60));p.drawEllipse(QRectF(s*.08,s*.08,s*.84,s*.84));
        const auto arc=[&](QPointF a,QPointF c,QPointF b,double width){
            QPainterPath path(a*s);path.quadTo(c*s,b*s);
            QPen pen(QColor(0x12,0x12,0x12));pen.setWidthF(width*s);pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawPath(path);
        };
        arc({.27,.39},{.52,.30},{.75,.43},.075);
        arc({.29,.53},{.51,.46},{.715,.56},.06);
        arc({.31,.665},{.50,.615},{.68,.69},.047);
        return image;
    }();
    return logo;
}

const QImage& jellyfinLogo() {
    static const QImage logo=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        // Jellyfin's mark: a rounded triangle with a smaller triangle cut from its centre, one gradient
        // running from violet (top) to blue (bottom right).
        const auto triangle=[](QPointF top,double width,double height,double round){
            const QPointF left(top.x()-width/2,top.y()+height),right(top.x()+width/2,top.y()+height);
            QPainterPath path;
            const auto toward=[](QPointF a,QPointF b,double d){const QPointF v=b-a;const double l=std::hypot(v.x(),v.y());return a+v*(d/l);};
            path.moveTo(toward(top,left,round));
            path.lineTo(toward(left,top,round));path.quadTo(left,toward(left,right,round));
            path.lineTo(toward(right,left,round));path.quadTo(right,toward(right,top,round));
            path.lineTo(toward(top,right,round));path.quadTo(top,toward(top,left,round));
            path.closeSubpath();return path;
        };
        QPainterPath outer=triangle({128,34},212,178,26);
        QPainterPath inner=triangle({128,104},96,80,12);
        QLinearGradient gradient(70,60,200,215);
        gradient.setColorAt(0,QColor(0xaa,0x5c,0xc3));gradient.setColorAt(1,QColor(0x00,0xa4,0xdc));
        p.setPen(Qt::NoPen);p.setBrush(gradient);
        p.drawPath(outer.subtracted(triangle({128,82},140,118,18)));
        p.drawPath(inner);
        return image;
    }();
    return logo;
}

const QImage& steamLogo() {
    static const QImage logo=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        // Steam's mark: a navy disc; on it a large ring (upper right) holding a dot, a small ring (lower left),
        // and the rod joining them, all white.
        QLinearGradient gradient(40,24,216,232);
        gradient.setColorAt(0,QColor(0x11,0x1d,0x2e));gradient.setColorAt(1,QColor(0x1f,0x5f,0x8f));
        p.setPen(Qt::NoPen);p.setBrush(gradient);p.drawEllipse(QRectF(20,20,216,216));
        const QColor white(250,250,250);
        QPen rod(white);rod.setWidthF(30);rod.setCapStyle(Qt::RoundCap);
        p.setPen(rod);p.drawLine(QPointF(98,166),QPointF(150,118));
        p.setPen(Qt::NoPen);p.setBrush(white);
        p.drawEllipse(QPointF(160,100),44,44);
        p.drawEllipse(QPointF(92,170),30,30);
        p.setBrush(QColor(0x17,0x34,0x52));
        p.drawEllipse(QPointF(160,100),30,30);
        p.drawEllipse(QPointF(92,170),17,17);
        p.setBrush(white);
        p.drawEllipse(QPointF(160,100),19,19);
        return image;
    }();
    return logo;
}

const QImage& tailscaleLogo() {
    static const QImage logo=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);p.setBrush(QColor(0x24,0x24,0x24));p.drawEllipse(QRectF(20,20,216,216));
        // Rows of three dots: the middle row and the bottom centre solid, the rest faded (Tailscale's mark).
        const bool solid[9]={false,false,false,true,true,true,false,true,false};
        for(int i=0;i<9;++i) {
            p.setBrush(solid[i]?QColor(250,250,250):QColor(250,250,250,90));
            p.drawEllipse(QPointF(78+50*(i%3),78+50*(i/3)),19,19);
        }
        return image;
    }();
    return logo;
}

const QImage& likedSongs() {
    static const QImage tile=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient gradient(28,28,228,228);gradient.setColorAt(0,QColor(0x45,0x0a,0xf5));gradient.setColorAt(1,QColor(0xc4,0xef,0xd9));
        p.setPen(Qt::NoPen);p.setBrush(gradient);p.drawRoundedRect(QRectF(28,28,200,200),14,14);
        QPainterPath heart;heart.moveTo(128,184);
        heart.cubicTo(76,150,66,118,80,98);heart.cubicTo(95,78,119,82,128,102);
        heart.cubicTo(137,82,161,78,176,98);heart.cubicTo(190,118,180,150,128,184);
        p.setBrush(Qt::white);p.drawPath(heart);
        return image;
    }();
    return tile;
}

const QImage& device(const QString& type) {
    static const QImage computer=line([](QPainter& p){p.drawRoundedRect(QRectF(48,60,160,108),8,8);p.drawLine(QPointF(128,168),QPointF(128,194));p.drawLine(QPointF(92,196),QPointF(164,196));});
    static const QImage phone=line([](QPainter& p){p.drawRoundedRect(QRectF(88,44,80,168),14,14);p.drawLine(QPointF(118,190),QPointF(138,190));});
    static const QImage tv=line([](QPainter& p){p.drawRoundedRect(QRectF(36,66,184,112),6,6);p.drawLine(QPointF(100,200),QPointF(156,200));});
    static const QImage speaker=line([](QPainter& p){p.drawRoundedRect(QRectF(72,40,112,176),18,18);p.drawEllipse(QPointF(128,150),30,30);p.drawEllipse(QPointF(128,80),10,10);});
    const QString t=type.toLower();
    if(t==QLatin1String("computer"))return computer;
    if(t==QLatin1String("smartphone")||t==QLatin1String("tablet"))return phone;
    if(t==QLatin1String("tv")||t==QLatin1String("castvideo")||t==QLatin1String("stb")||t==QLatin1String("gameconsole"))return tv;
    return speaker;
}

const QImage& signOut() {
    static const QImage image=line([](QPainter& p){
        p.drawPolyline(QPolygonF{{120,56},{60,56},{60,200},{120,200}});
        p.drawLine(QPointF(100,128),QPointF(200,128));
        p.drawPolyline(QPolygonF{{168,96},{200,128},{168,160}});
    });
    return image;
}

const QImage& search() {
    static const QImage image=line([](QPainter& p){p.drawEllipse(QPointF(112,112),52,52);p.drawLine(QPointF(150,150),QPointF(200,200));});
    return image;
}

const QImage& more() {
    static const QImage image=line([](QPainter& p){
        p.setBrush(QColor(250,250,250));
        for(const double x:{76.,128.,180.})p.drawEllipse(QPointF(x,128),9,9);
    });
    return image;
}

const QImage& download() {
    static const QImage image=line([](QPainter& p){
        p.drawLine(QPointF(128,44),QPointF(128,152));
        p.drawPolyline(QPolygonF{{84,110},{128,154},{172,110}});
        p.drawPolyline(QPolygonF{{52,160},{52,206},{204,206},{204,160}});
    });
    return image;
}

const QImage& family() {
    static const QImage image=line([](QPainter& p){
        p.drawEllipse(QPointF(96,84),24,24);p.drawEllipse(QPointF(168,96),20,20);
        QPainterPath a;a.moveTo(52,200);a.cubicTo(52,136,140,136,140,200);p.drawPath(a);
        QPainterPath b;b.moveTo(132,200);b.cubicTo(132,148,204,148,204,200);p.drawPath(b);
    });
    return image;
}

const QImage& library(const QString& collectionType) {
    static const QImage none;
    static const QImage movies=line([](QPainter& p){
        p.drawRoundedRect(QRectF(44,76,168,124),8,8);
        p.drawPolyline(QPolygonF{{44,76},{60,44},{212,44},{212,76}});
        for(const double x:{92.,140.,188.})p.drawLine(QPointF(x-24,44),QPointF(x-40,76));
    });
    static const QImage shows=line([](QPainter& p){
        p.drawRoundedRect(QRectF(36,72,184,120),8,8);
        p.drawLine(QPointF(96,40),QPointF(128,70));p.drawLine(QPointF(160,40),QPointF(128,70));
        p.drawLine(QPointF(96,214),QPointF(160,214));
    });
    static const QImage music=line([](QPainter& p){
        p.drawLine(QPointF(100,186),QPointF(100,58));p.drawLine(QPointF(100,58),QPointF(188,40));p.drawLine(QPointF(188,40),QPointF(188,166));
        p.setBrush(QColor(250,250,250));
        p.drawEllipse(QPointF(78,190),24,18);p.drawEllipse(QPointF(166,170),24,18);
    });
    static const QImage lists=line([](QPainter& p){
        for(const double y:{72.,128.,184.}){p.drawLine(QPointF(96,y),QPointF(208,y));p.drawPoint(QPointF(56,y));}
    });
    static const QImage photos=line([](QPainter& p){
        p.drawRoundedRect(QRectF(40,60,176,136),10,10);
        p.drawPolyline(QPolygonF{{56,180},{108,124},{148,164},{170,142},{200,180}});
        p.drawEllipse(QPointF(170,98),14,14);
    });
    const QString t=collectionType.toLower();
    if(t==QLatin1String("movies")||t==QLatin1String("boxsets"))return movies;
    if(t==QLatin1String("tvshows")||t==QLatin1String("livetv"))return shows;
    if(t==QLatin1String("music")||t==QLatin1String("musicvideos"))return music;
    if(t==QLatin1String("playlists"))return lists;
    if(t==QLatin1String("homevideos")||t==QLatin1String("photos"))return photos;
    return none;
}
}
