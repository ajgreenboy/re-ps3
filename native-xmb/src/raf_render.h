#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

namespace xmb {
struct RafStillStats {
    int actors=0,models=0,textures=0,triangles=0;
    bool cameraFallback=false,lightFallback=false;
};
QImage renderRafStill(const QByteArray& raf,const QString& devFlash,int width,int height,QString* error=nullptr,RafStillStats* stats=nullptr);
}
