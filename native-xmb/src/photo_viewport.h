#pragma once

#include <QRectF>
#include <QSizeF>

namespace xmb {

// Pure geometry state for the photo viewer; no rendering or image ownership.
class PhotoViewport {
public:
    void reset(QSizeF image, QSizeF viewport, bool fill);
    void resetView();
    bool zoom(int direction);
    bool pan(int dx, int dy);

    QRectF imageRect() const;
    bool canPan() const;
    double zoomPercent() const;
    bool valid() const;

private:
    double scale_ = 0.0;
    double fit_ = 0.0;
    double fill_ = 0.0;
    double base_ = 0.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
    QSizeF image_;
    QSizeF viewport_;
};

} // namespace xmb
