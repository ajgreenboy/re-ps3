#pragma once
#include "assets.h"
#include "gallery_controller.h"
#include "gallery_thumbs.h"
#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QString>
#include <QVector4D>
#include <memory>

namespace xmb {
// Photo Gallery GL scene (audit/PHOTO-GALLERY-DESIGN.md step G4), modelled on Coldboot: init(gl, assets) once, then
// render(...) into a target framebuffer. It draws the floor, the piles' photos with shadows, the contour, date and
// year labels, the arrows between piles, the focus ring and the grid cursor, from the controller's camera and draws.
// Every constant is read from parameters.txt through the controller's GalleryParams; every texture comes from
// dev_flash (vsh/resource/hknw_plugin/*.png, the RCO's tex_arrow_anim) at run time. What the firmware's own floors
// (the NPIA00006 package) would add is absent; the floor is one lit quad (see audit/PHOTO-GALLERY.md).
class GalleryScene {
public:
    struct Stats {
        int quads = 0, photos = 0, textures = 0, uploads = 0, requested = 0, glErrors = 0, labels = 0;
        QString firstError;
    };
    GalleryScene();
    // The parameters.txt names the scene reads (the caller checks them with GalleryParams::require).
    static QStringList paramNames();
    ~GalleryScene();
    bool init(QOpenGLFunctions_3_3_Core& gl, Assets& assets);
    bool ready() const { return ready_; }
    // The thumbnail cache for this session (not owned) and the font the labels are drawn with.
    void attach(GalleryThumbs* thumbs, const QString& fontFamily);
    // Drops per-session GL objects (photo textures, labels): a new library or a new sort.
    void reset(QOpenGLFunctions_3_3_Core& gl);
    void render(QOpenGLFunctions_3_3_Core& gl, int pixelWidth, int pixelHeight, GLuint targetFbo, const GalleryController& controller,
                double seconds);
    const Stats& stats() const { return stats_; }
    // Photos whose thumbnail is waiting on the GL side or in flight (the "loading" indicator and the shot flag's settle test).
    int pendingThumbnails() const { return pending_; }
    // The view-projection of the last render, for chrome that anchors to the floor.
    QMatrix4x4 lastViewProjection() const { return viewProj_; }

private:
    struct Tex { std::shared_ptr<QOpenGLTexture> texture; int level = 0; float aspect = 4.f / 3.f; quint64 used = 0; };
    struct Label { std::shared_ptr<QOpenGLTexture> texture; float aspect = 4; };
    struct Uniforms { int viewProj = -1, model = -1, mode = -1, tex = -1, tex2 = -1, tint = -1, uvRect = -1, lightPos = -1, lightK = -1,
        camPos = -1, floorColor = -1, floorOut = -1, floorParams = -1, lightCentre = -1, ab = -1; };

    void drawQuad(const QMatrix4x4& model, int mode, const QVector4D& tint, GLuint tex = 0, GLuint tex2 = 0,
                  const QVector4D& uvRect = QVector4D(0, 0, 1, 1), float abX = 1, float abY = 1);
    std::unique_ptr<QOpenGLTexture> upload(const QImage& image, bool mipmaps, bool repeat);
    QImage loadFirmwarePng(const QString& name) const;
    const Label* labelFor(qint64 key, const QString& text, int pixelHeight, double maxAspect = 0);
    void checkGl(const char* where);
    GLuint photoTexture(const QString& path, float* aspect);
    void planThumbnails(const GalleryController& controller, const QVector<GalleryDraw>& draws, const GalleryCamera& camera);

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    Assets* assets_ = nullptr;
    GalleryThumbs* thumbs_ = nullptr;
    QString font_;
    bool ready_ = false;
    QOpenGLShaderProgram program_;
    Uniforms u_;
    GLuint vao_ = 0, vbo_ = 0;
    std::unique_ptr<QOpenGLTexture> white_, grain_, shadow_[3], envmap_, focusCluster_, focusClusterShadow_, focusGrid_, contour_, arrow_;
    int arrowFrames_ = 1;
    QHash<QString, Tex> photoTextures_;
    QHash<QString, int> bestLevel_;
    QHash<qint64, Label> labels_;
    QHash<qint64, Label> yearLabels_;
    QMatrix4x4 viewProj_;
    quint64 tick_ = 0;
    int pending_ = 0, frame_ = 0;
    Stats stats_;
    bool errorReported_ = false;
};
}
