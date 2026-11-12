#pragma once
#include <QSizeF>
#include <QTransform>
#include <QOpenGLFunctions_3_3_Core>
#include <algorithm>

namespace xmb {
// Uniform, centred contain-fit: the whole 1920x1080 design area is always visible and undistorted; a window that is
// not 16:9 gets bars (par2 sweep: the earlier crop-fill cut the clock, the item list and the player controls off
// on ultrawide and narrow windows).
struct DesignSpace {
    explicit DesignSpace(qreal width,qreal height):x(std::min(width/1920.,height/1080.)),y(x),
        left((width-1920.*x)/2.),top((height-1080.*y)/2.) {}
    explicit DesignSpace(const QSizeF& size):DesignSpace(size.width(),size.height()) {}
    QTransform transform() const { return QTransform(x,0,0,y,left,top); }
    void viewport(QOpenGLFunctions_3_3_Core& gl) const {
        gl.glViewport(qRound(left),qRound(top),qRound(1920.*x),qRound(1080.*y));
    }
    qreal x,y,left,top;
};
}
