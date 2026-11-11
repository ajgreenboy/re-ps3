#pragma once
#include <QSizeF>
#include <QTransform>

namespace xmb {
// Keep firmware coordinates while stretching every design-space surface to the window.
struct DesignSpace {
    explicit DesignSpace(qreal width,qreal height):x(width/1920.),y(height/1080.) {}
    explicit DesignSpace(const QSizeF& size):DesignSpace(size.width(),size.height()) {}
    QTransform transform() const { return QTransform::fromScale(x,y); }
    qreal x,y;
};
}
