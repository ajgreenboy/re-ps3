#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <memory>

namespace xmb {
// Presentation only: Qt retains focus and renders the premultiplied GL framebuffer.
// No capture, injection, or game-process access is involved in live composition.
class LiveOverlay {
public:
    LiveOverlay();
    ~LiveOverlay();
    bool create(quintptr owner, const QRect& monitor, QString* error);
    bool present(const QImage& premultipliedBgra, QString* error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace xmb
