#include "photo_viewport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xmb {
namespace {

bool finitePositive(double value) {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

void PhotoViewport::reset(QSizeF image, QSizeF viewport, bool fill) {
    image_ = image;
    viewport_ = viewport;
    panX_ = 0.0;
    panY_ = 0.0;
    scale_ = fit_ = fill_ = base_ = 0.0;
    if (!finitePositive(image.width()) || !finitePositive(image.height()) ||
        !finitePositive(viewport.width()) || !finitePositive(viewport.height())) {
        return;
    }

    const double sx = viewport.width() / image.width();
    const double sy = viewport.height() / image.height();
    fit_ = std::min(sx, sy);
    fill_ = std::max(sx, sy);
    if (!finitePositive(fit_) || !finitePositive(fill_)) return;
    base_ = fill ? fill_ : fit_;
    scale_ = base_;
}

void PhotoViewport::resetView() {
    if (!valid()) return;
    scale_ = base_;
    panX_ = panY_ = 0.0;
}

bool PhotoViewport::zoom(int direction) {
    if (!valid() || direction == 0) return false;

    const double minimum = std::min(1.0, fit_);
    const double maximum = std::max({2.0, fill_, fit_});
    double target = scale_;
    auto consider = [&](double stop) {
        if (!finitePositive(stop) || stop < minimum || stop > maximum) return;
        if (direction > 0 && stop > scale_ && (target == scale_ || stop < target)) target = stop;
        if (direction < 0 && stop < scale_ && (target == scale_ || stop > target)) target = stop;
    };

    consider(minimum);
    consider(maximum);
    consider(fit_);
    consider(fill_);

    // Integer 100% stops are found arithmetically, avoiding an unbounded list
    // when image dimensions are tiny and the viewport is exceptionally large.
    const double integerStop = direction > 0 ? std::floor(scale_) + 1.0
                                             : std::ceil(scale_) - 1.0;
    consider(integerStop);
    if (target == scale_) return false;

    const double ratio = target / scale_;
    scale_ = target;
    panX_ *= ratio;
    panY_ *= ratio;
    const double limitX = std::max(0.0, (image_.width() * scale_ - viewport_.width()) * 0.5);
    const double limitY = std::max(0.0, (image_.height() * scale_ - viewport_.height()) * 0.5);
    panX_ = std::clamp(panX_, -limitX, limitX);
    panY_ = std::clamp(panY_, -limitY, limitY);
    return true;
}

bool PhotoViewport::pan(int dx, int dy) {
    if (!valid() || (dx == 0 && dy == 0)) return false;
    const double step = 0.05 * std::min(viewport_.width(), viewport_.height());
    const double limitX = std::max(0.0, (image_.width() * scale_ - viewport_.width()) * 0.5);
    const double limitY = std::max(0.0, (image_.height() * scale_ - viewport_.height()) * 0.5);
    const double nextX = std::clamp(panX_ + static_cast<double>(dx) * step, -limitX, limitX);
    const double nextY = std::clamp(panY_ + static_cast<double>(dy) * step, -limitY, limitY);
    if (nextX == panX_ && nextY == panY_) return false;
    panX_ = nextX;
    panY_ = nextY;
    return true;
}

QRectF PhotoViewport::imageRect() const {
    if (!valid()) return {};
    const double width = image_.width() * scale_;
    const double height = image_.height() * scale_;
    return {viewport_.width() * 0.5 + panX_ - width * 0.5,
            viewport_.height() * 0.5 + panY_ - height * 0.5, width, height};
}

bool PhotoViewport::canPan() const {
    if (!valid()) return false;
    return image_.width() * scale_ > viewport_.width()+.001 || image_.height() * scale_ > viewport_.height()+.001;
}

double PhotoViewport::zoomPercent() const {
    return valid() ? scale_ * 100.0 : 0.0;
}

bool PhotoViewport::valid() const {
    return finitePositive(scale_) && finitePositive(image_.width()) && finitePositive(image_.height()) &&
           finitePositive(viewport_.width()) && finitePositive(viewport_.height());
}

} // namespace xmb
