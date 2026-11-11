#pragma once
#include <QSizeF>
#include <QTransform>
#include <QOpenGLFunctions_3_3_Core>
#include <algorithm>

namespace xmb {
// Uniform, centred crop-fill: no bars and no aspect distortion, including on non-16:9 displays.
struct DesignSpace {
    explicit DesignSpace(qreal width,qreal height):x(std::max(width/1920.,height/1080.)),y(x),
        left((width-1920.*x)/2.),top((height-1080.*y)/2.) {}
    explicit DesignSpace(const QSizeF& size):DesignSpace(size.width(),size.height()) {}
    QTransform transform() const { return QTransform(x,0,0,y,left,top); }
    void viewport(QOpenGLFunctions_3_3_Core& gl) const {
        gl.glViewport(qRound(left),qRound(top),qRound(1920.*x),qRound(1080.*y));
    }
    qreal x,y,left,top;
};
}
